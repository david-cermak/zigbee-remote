/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for zigbee-remote: clear observable link state on every failure.
 */
#include "ppp_link.h"
#include "netdev_fwd.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lwip/dns.h"
#include "lwip/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip6.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "netif/ppp/ppp.h"
#include "netif/ppp/pppos.h"
#if PPP_IPV6_SUPPORT
#include "netif/ppp/ipv6cp.h"
#endif

#define BUF_SIZE 1518

void ppp_init(void);
err_t lwip_ip4_input(struct pbuf *p, struct netif *inp);

static const unsigned char ip6_header[4] = { 0, 0, 0x86, 0xdd };
static const unsigned char ip4_header[4] = { 0, 0, 0x08, 0 };

static struct ppp_link_io *s_io;
static struct netif pppos_netif;
static ppp_pcb *ppp;
static volatile int s_link_up;
static int s_use_tcpip;

static void ppp_link_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    struct netif *pppif = ppp_netif(pcb);
    LWIP_UNUSED_ARG(ctx);

    if (err_code != PPPERR_NONE) {
        s_link_up = 0;
    }
    switch (err_code) {
    case PPPERR_NONE: {
#if LWIP_DNS
        const ip_addr_t *ns;
#endif
        fprintf(stderr, "ppp: link up\n");
#if LWIP_IPV4
        fprintf(stderr, "   local  %s\n", ip4addr_ntoa(netif_ip4_addr(pppif)));
        fprintf(stderr, "   peer   %s\n", ip4addr_ntoa(netif_ip4_gw(pppif)));
        fprintf(stderr, "   mask   %s\n", ip4addr_ntoa(netif_ip4_netmask(pppif)));
#endif
#if LWIP_DNS
        ns = dns_getserver(0);
        fprintf(stderr, "   dns1   %s\n", ipaddr_ntoa(ns));
        ns = dns_getserver(1);
        fprintf(stderr, "   dns2   %s\n", ipaddr_ntoa(ns));
#endif
#if LWIP_IPV6
        fprintf(stderr, "   local6 %s\n", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
#endif
        s_link_up = 1;
#if LWIP_IPV4
        if (netdev_fwd_enabled() && !ip4_addr_isany_val(*netif_ip4_addr(pppif))) {
            netdev_fwd_on_link_up();
        }
#endif
        break;
    }
    case PPPERR_PARAM:
        fprintf(stderr, "ppp: PPPERR_PARAM\n");
        break;
    case PPPERR_OPEN:
        fprintf(stderr, "ppp: PPPERR_OPEN\n");
        break;
    case PPPERR_DEVICE:
        fprintf(stderr, "ppp: PPPERR_DEVICE\n");
        break;
    case PPPERR_ALLOC:
        fprintf(stderr, "ppp: PPPERR_ALLOC\n");
        break;
    case PPPERR_USER:
        fprintf(stderr, "ppp: PPPERR_USER\n");
        break;
    case PPPERR_CONNECT:
        fprintf(stderr, "ppp: PPPERR_CONNECT\n");
        break;
    case PPPERR_AUTHFAIL:
        fprintf(stderr, "ppp: PPPERR_AUTHFAIL\n");
        break;
    case PPPERR_PROTOCOL:
        fprintf(stderr, "ppp: PPPERR_PROTOCOL\n");
        break;
    case PPPERR_PEERDEAD:
        fprintf(stderr, "ppp: PPPERR_PEERDEAD\n");
        break;
    case PPPERR_IDLETIMEOUT:
        fprintf(stderr, "ppp: PPPERR_IDLETIMEOUT\n");
        break;
    case PPPERR_CONNECTTIME:
        fprintf(stderr, "ppp: PPPERR_CONNECTTIME\n");
        break;
    case PPPERR_LOOPBACK:
        fprintf(stderr, "ppp: PPPERR_LOOPBACK\n");
        break;
    default:
        fprintf(stderr, "ppp: error %d\n", err_code);
        break;
    }
}

static u32_t ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx)
{
    struct ppp_link_io *io = (struct ppp_link_io *)ctx;
    LWIP_UNUSED_ARG(pcb);
    if (io && io->output) {
        ssize_t n = io->output(io->output_ctx, data, len);
        return n > 0 ? (u32_t)n : 0;
    }
    return 0;
}

void ppp_link_serial_input(const uint8_t *data, size_t len)
{
    if (!ppp) {
        return;
    }
    if (s_use_tcpip) {
        pppos_input_tcpip(ppp, (void *)data, (int)len);
    } else {
        pppos_input(ppp, (void *)data, (int)len);
    }
}

static int configure_ppp_server(ppp_pcb *pcb, const ppp_config_t *cfg)
{
    ip4_addr_t local_addr;
    ip4_addr_t peer_addr;
    ip4_addr_t dns_addr;

    if (!cfg) {
        return 0;
    }

    if (!ip4addr_aton(cfg->local_ip, &local_addr)) {
        fprintf(stderr, "Invalid local IP: %s\n", cfg->local_ip);
        return 0;
    }
    if (!ip4addr_aton(cfg->peer_ip, &peer_addr)) {
        fprintf(stderr, "Invalid peer IP: %s\n", cfg->peer_ip);
        return 0;
    }
    if (!ip4addr_aton(cfg->dns_ip, &dns_addr)) {
        fprintf(stderr, "Invalid DNS IP: %s\n", cfg->dns_ip);
        return 0;
    }

    ppp_set_passive(pcb, 1);

    pcb->ipcp_wantoptions.ouraddr = local_addr.addr;
    pcb->ipcp_wantoptions.accept_remote = 1;
    pcb->ask_for_local = 1;

    pcb->ipcp_wantoptions.hisaddr = peer_addr.addr;
    pcb->ipcp_wantoptions.accept_local = 1;

    ppp_set_ipcp_dnsaddr(pcb, 0, &dns_addr);
    ppp_set_usepeerdns(pcb, 0);

#if VJ_SUPPORT
    /* VJ + userspace TCP splice is fragile; leave off in netdev mode */
    if (!netdev_fwd_enabled()) {
        pcb->ipcp_wantoptions.neg_vj = 1;
        pcb->ipcp_allowoptions.neg_vj = 1;
    } else {
        pcb->ipcp_wantoptions.neg_vj = 0;
        pcb->ipcp_allowoptions.neg_vj = 0;
    }
#endif

#if PPP_IPV6_SUPPORT
    if (cfg->ipv6) {
        pcb->ipv6cp_wantoptions.neg_ifaceid = 1;
        pcb->ipv6cp_allowoptions.neg_ifaceid = 1;
        pcb->ipv6cp_wantoptions.accept_local = 1;
        pcb->ipv6cp_allowoptions.accept_local = 1;
        pcb->ipv6cp_wantoptions.use_ip = 1;
    } else {
        pcb->ipv6cp_wantoptions.neg_ifaceid = 0;
        pcb->ipv6cp_allowoptions.neg_ifaceid = 0;
    }
#endif

    return 1;
}

static int configure_ppp_client(ppp_pcb *pcb, const ppp_config_t *cfg)
{
    if (!cfg) {
        return 0;
    }

    ppp_set_usepeerdns(pcb, 1);

#if VJ_SUPPORT
    if (!netdev_fwd_enabled()) {
        pcb->ipcp_wantoptions.neg_vj = 1;
    } else {
        pcb->ipcp_wantoptions.neg_vj = 0;
    }
#endif

#if PPP_IPV6_SUPPORT
    if (cfg->ipv6) {
        pcb->ipv6cp_wantoptions.neg_ifaceid = 1;
        pcb->ipv6cp_allowoptions.neg_ifaceid = 1;
    } else {
        pcb->ipv6cp_wantoptions.neg_ifaceid = 0;
        pcb->ipv6cp_allowoptions.neg_ifaceid = 0;
    }
#endif

    return 1;
}

int ppp_link_is_up(void)
{
    return s_link_up;
}

struct ppp_setup_ctx {
    int ok;
    sys_sem_t done;
};

static void ppp_setup_tcpip(void *arg)
{
    struct ppp_setup_ctx *ctx = (struct ppp_setup_ctx *)arg;

    ppp = pppos_create(&pppos_netif, ppp_output_cb, ppp_link_status_cb, s_io);
    if (!ppp) {
        ctx->ok = 0;
        sys_sem_signal(&ctx->done);
        return;
    }

    if (s_io->ppp_cfg->mode == PPP_LINK_MODE_SERVER) {
        if (!configure_ppp_server(ppp, s_io->ppp_cfg) || ppp_listen(ppp) != ERR_OK) {
            fprintf(stderr, "ppp_listen failed\n");
            ctx->ok = 0;
            sys_sem_signal(&ctx->done);
            return;
        }
    } else {
        if (!configure_ppp_client(ppp, s_io->ppp_cfg) || ppp_connect(ppp, 0) != ERR_OK) {
            fprintf(stderr, "ppp_connect failed\n");
            ctx->ok = 0;
            sys_sem_signal(&ctx->done);
            return;
        }
    }

    ctx->ok = 1;
    sys_sem_signal(&ctx->done);
}

int ppp_link_lwip_init(ppp_link_t *link)
{
    s_io = ppp_link_io(link);
    if (!s_io) {
        return 0;
    }

    s_use_tcpip = netdev_fwd_enabled() || (s_io->ppp_cfg && s_io->ppp_cfg->netdev);

    if (s_use_tcpip) {
        struct ppp_setup_ctx ctx;

        tcpip_init(NULL, NULL);
        memset(&ctx, 0, sizeof(ctx));
        if (sys_sem_new(&ctx.done, 0) != ERR_OK) {
            return 0;
        }
        if (tcpip_callback(ppp_setup_tcpip, &ctx) != ERR_OK) {
            sys_sem_free(&ctx.done);
            return 0;
        }
        sys_sem_wait(&ctx.done);
        sys_sem_free(&ctx.done);
        return ctx.ok;
    }

    sys_init();
    mem_init();
    memp_init();
    netif_init();
    dns_init();
    ppp_init();
    sys_timeouts_init();

    ppp = pppos_create(&pppos_netif, ppp_output_cb, ppp_link_status_cb, s_io);
    if (!ppp) {
        return 0;
    }

    if (s_io->ppp_cfg->mode == PPP_LINK_MODE_SERVER) {
        if (!configure_ppp_server(ppp, s_io->ppp_cfg)) {
            return 0;
        }
        if (ppp_listen(ppp) != ERR_OK) {
            fprintf(stderr, "ppp_listen failed\n");
            return 0;
        }
    } else {
        if (!configure_ppp_client(ppp, s_io->ppp_cfg)) {
            return 0;
        }
        if (ppp_connect(ppp, 0) != ERR_OK) {
            fprintf(stderr, "ppp_connect failed\n");
            return 0;
        }
    }

    return 1;
}

static err_t tun_output(struct pbuf *p, const unsigned char tun_header[4])
{
    struct pbuf *n;
    size_t offset = 4;

    if (!s_io) {
        pbuf_free(p);
        return ERR_ARG;
    }

    if (s_io->tun_fd < 0) {
        pbuf_free(p);
        return ERR_OK;
    }

    memcpy(s_io->in_buf, tun_header, offset);
    for (n = p; n; n = n->next) {
        memcpy(s_io->in_buf + offset, n->payload, n->len);
        offset += n->len;
    }
    if (write(s_io->tun_fd, s_io->in_buf, offset) != (ssize_t)offset) {
        pbuf_free(p);
        return ERR_ABRT;
    }
    pbuf_free(p);
    return ERR_OK;
}

err_t ip6_input(struct pbuf *p, struct netif *inp)
{
    LWIP_UNUSED_ARG(inp);
    return tun_output(p, ip6_header);
}

err_t ip4_input(struct pbuf *p, struct netif *inp)
{
    if (netdev_fwd_enabled()) {
        return lwip_ip4_input(p, inp);
    }
    LWIP_UNUSED_ARG(inp);
    return tun_output(p, ip4_header);
}

int ppp_link_tun_poll(ppp_link_t *link)
{
    struct ppp_link_io *io = ppp_link_io(link);
    fd_set fds;
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

    if (!io) {
        return 0;
    }

    if (io->tun_fd < 0) {
        usleep(100000);
        if (!s_use_tcpip) {
            sys_check_timeouts();
        }
        return 0;
    }

    FD_ZERO(&fds);
    FD_SET(io->tun_fd, &fds);

    if (select(io->tun_fd + 1, &fds, NULL, NULL, &tv) <= 0) {
        if (!s_use_tcpip) {
            sys_check_timeouts();
        }
        return 0;
    }

    ssize_t len = read(io->tun_fd, io->out_buf, BUF_SIZE);
    if (len < 0) {
        perror("tun read");
        return -1;
    }
    if (len <= 4) {
        return 0;
    }
    len -= 4;

    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (!p) {
        perror("pbuf_alloc");
        return -1;
    }
    pbuf_take(p, io->out_buf + 4, len);

    if (memcmp(io->out_buf, ip6_header, 4) == 0) {
        pppos_netif.output_ip6(&pppos_netif, p, NULL);
    } else if (memcmp(io->out_buf, ip4_header, 4) == 0) {
        pppos_netif.output(&pppos_netif, p, NULL);
    } else {
        fprintf(stderr, "tun: unknown ethertype %02x%02x\n", io->out_buf[2], io->out_buf[3]);
    }
    pbuf_free(p);
    return 1;
}
