/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * QEMU-user-style TCP forward: ESP connects to PPP local:port, we splice
 * to a host TCP endpoint (default 127.0.0.1:port).
 */
#include "netdev_fwd.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lwip/ip4_addr.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"

#define NETDEV_BUF 2048

struct netdev_conn {
    struct tcp_pcb *pcb;
    int host_fd;
    pthread_t thread;
    volatile int alive;
};

struct write_req {
    struct tcp_pcb *pcb;
    const void *data;
    uint16_t len;
    err_t err;
    sys_sem_t done;
};

static char s_host[256] = "127.0.0.1";
static uint16_t s_port;
static int s_enabled;
static int s_listening;

void netdev_fwd_configure(const char *host, uint16_t port)
{
    if (host && host[0]) {
        snprintf(s_host, sizeof(s_host), "%s", host);
    }
    s_port = port;
    s_enabled = (port != 0);
}

int netdev_fwd_enabled(void)
{
    return s_enabled;
}

static err_t netdev_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct netdev_conn *c = (struct netdev_conn *)arg;

    if (!c) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_ARG;
    }

    if (err != ERR_OK) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (!p) {
        c->alive = 0;
        if (c->host_fd >= 0) {
            shutdown(c->host_fd, SHUT_WR);
        }
        return ERR_OK;
    }

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        const uint8_t *data = (const uint8_t *)q->payload;
        size_t left = q->len;
        while (left > 0 && c->host_fd >= 0 && c->alive) {
            ssize_t n = write(c->host_fd, data, left);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                c->alive = 0;
                break;
            }
            data += n;
            left -= (size_t)n;
        }
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void netdev_err(void *arg, err_t err)
{
    struct netdev_conn *c = (struct netdev_conn *)arg;
    LWIP_UNUSED_ARG(err);
    if (!c) {
        return;
    }
    c->pcb = NULL;
    c->alive = 0;
    if (c->host_fd >= 0) {
        shutdown(c->host_fd, SHUT_RDWR);
    }
}

static void do_tcp_write(void *arg)
{
    struct write_req *r = (struct write_req *)arg;
    r->err = tcp_write(r->pcb, r->data, r->len, TCP_WRITE_FLAG_COPY);
    if (r->err == ERR_OK) {
        tcp_output(r->pcb);
    }
    sys_sem_signal(&r->done);
}

static err_t tcp_write_sync(struct tcp_pcb *pcb, const void *data, uint16_t len)
{
    struct write_req r;

    memset(&r, 0, sizeof(r));
    r.pcb = pcb;
    r.data = data;
    r.len = len;
    r.err = ERR_VAL;

    if (sys_sem_new(&r.done, 0) != ERR_OK) {
        return ERR_MEM;
    }
    if (tcpip_callback(do_tcp_write, &r) != ERR_OK) {
        sys_sem_free(&r.done);
        return ERR_VAL;
    }
    sys_arch_sem_wait(&r.done, 0);
    sys_sem_free(&r.done);
    return r.err;
}

static void close_pcb_cb(void *arg)
{
    struct tcp_pcb *p = (struct tcp_pcb *)arg;
    tcp_arg(p, NULL);
    tcp_recv(p, NULL);
    tcp_err(p, NULL);
    tcp_close(p);
}

static void *host_to_pcb_thread(void *arg)
{
    struct netdev_conn *c = (struct netdev_conn *)arg;
    uint8_t buf[NETDEV_BUF];

    while (c->alive && c->host_fd >= 0 && c->pcb != NULL) {
        ssize_t n = read(c->host_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }

        uint16_t off = 0;
        while (off < (uint16_t)n && c->alive && c->pcb != NULL) {
            uint16_t chunk = (uint16_t)n - off;
            err_t e = tcp_write_sync(c->pcb, buf + off, chunk);
            if (e == ERR_MEM) {
                usleep(2000);
                continue;
            }
            if (e != ERR_OK) {
                c->alive = 0;
                break;
            }
            off += chunk;
        }
    }

    c->alive = 0;
    if (c->pcb != NULL) {
        struct tcp_pcb *pcb = c->pcb;
        c->pcb = NULL;
        tcpip_callback(close_pcb_cb, pcb);
    }
    if (c->host_fd >= 0) {
        close(c->host_fd);
        c->host_fd = -1;
    }
    free(c);
    return NULL;
}

static int linux_connect_host(void)
{
    char port_str[16];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    int fd = -1;
    int gai;

    snprintf(port_str, sizeof(port_str), "%u", (unsigned)s_port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    gai = getaddrinfo(s_host, port_str, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "netdev: resolve %s: %s\n", s_host, gai_strerror(gai));
        return -1;
    }

    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            char peer[INET6_ADDRSTRLEN];
            const void *addr_ptr = NULL;
            if (ai->ai_family == AF_INET) {
                addr_ptr = &((struct sockaddr_in *)ai->ai_addr)->sin_addr;
            } else if (ai->ai_family == AF_INET6) {
                addr_ptr = &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
            }
            if (addr_ptr && inet_ntop(ai->ai_family, addr_ptr, peer, sizeof(peer))) {
                fprintf(stderr, "netdev: connected to %s (%s):%u\n",
                        s_host, peer, (unsigned)s_port);
            }
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "netdev: connect to %s:%u failed\n", s_host, (unsigned)s_port);
        return -1;
    }
    return fd;
}

static err_t netdev_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    LWIP_UNUSED_ARG(arg);

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    int host_fd = linux_connect_host();
    if (host_fd < 0) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    struct netdev_conn *c = (struct netdev_conn *)calloc(1, sizeof(*c));
    if (!c) {
        close(host_fd);
        tcp_abort(newpcb);
        return ERR_MEM;
    }

    c->pcb = newpcb;
    c->host_fd = host_fd;
    c->alive = 1;

    tcp_arg(newpcb, c);
    tcp_recv(newpcb, netdev_recv);
    tcp_err(newpcb, netdev_err);
    tcp_nagle_disable(newpcb);

    fprintf(stderr, "netdev: ESP %s:%u -> %s:%u\n",
            ip4addr_ntoa(ip_2_ip4(&newpcb->remote_ip)),
            (unsigned)newpcb->remote_port, s_host, (unsigned)s_port);

    if (pthread_create(&c->thread, NULL, host_to_pcb_thread, c) != 0) {
        perror("netdev pthread_create");
        close(host_fd);
        free(c);
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    pthread_detach(c->thread);
    return ERR_OK;
}

static void netdev_start_listen(void *arg)
{
    LWIP_UNUSED_ARG(arg);

    if (s_listening || !s_enabled) {
        return;
    }

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        fprintf(stderr, "netdev: tcp_new failed\n");
        return;
    }

    err_t e = tcp_bind(pcb, IP4_ADDR_ANY, s_port);
    if (e != ERR_OK) {
        fprintf(stderr, "netdev: tcp_bind(%u) failed: %d\n", (unsigned)s_port, (int)e);
        tcp_close(pcb);
        return;
    }

    struct tcp_pcb *listen_pcb = tcp_listen(pcb);
    if (!listen_pcb) {
        fprintf(stderr, "netdev: tcp_listen failed\n");
        tcp_close(pcb);
        return;
    }

    tcp_accept(listen_pcb, netdev_accept);
    s_listening = 1;
    fprintf(stderr, "netdev: listening on PPP *:%u -> %s:%u\n",
            (unsigned)s_port, s_host, (unsigned)s_port);
}

void netdev_fwd_on_link_up(void)
{
    if (!s_enabled || s_listening) {
        return;
    }
    tcpip_callback(netdev_start_listen, NULL);
}
