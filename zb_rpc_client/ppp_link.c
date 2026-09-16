/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ppp_link.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/if_tun.h>

#define BUF_SIZE 1518

struct ppp_link {
    struct ppp_link_io io;
    uint8_t in_buf[BUF_SIZE];
    uint8_t out_buf[BUF_SIZE];
    pthread_t tun_thread;
    volatile int running;
};

static void *tun_thread(void *arg)
{
    ppp_link_t *link = (ppp_link_t *)arg;

    while (link->running) {
        ppp_link_tun_poll(link);
    }
    return NULL;
}

ppp_link_t *ppp_link_create(const ppp_link_config_t *config)
{
    if (!config || !config->ppp) {
        return NULL;
    }

    ppp_link_t *link = calloc(1, sizeof(*link));
    if (!link) {
        return NULL;
    }

    link->io.in_buf = link->in_buf;
    link->io.out_buf = link->out_buf;
    link->io.ppp_cfg = config->ppp;
    link->io.tun_fd = -1;

    if (config->tun_dev && config->tun_if) {
        link->io.tun_fd = open(config->tun_dev, O_RDWR);
        if (link->io.tun_fd < 0) {
            perror(config->tun_dev);
            free(link);
            return NULL;
        }

        struct ifreq ifr = {};
        ifr.ifr_flags = IFF_TUN;
        strncpy(ifr.ifr_name, config->tun_if, IFNAMSIZ);

        if (ioctl(link->io.tun_fd, TUNSETIFF, &ifr) < 0) {
            perror("TUNSETIFF");
            close(link->io.tun_fd);
            free(link);
            return NULL;
        }
        ioctl(link->io.tun_fd, TUNSETNOCSUM, 1);
    }

    if (!ppp_link_lwip_init(link)) {
        if (link->io.tun_fd >= 0) {
            close(link->io.tun_fd);
        }
        free(link);
        return NULL;
    }

    link->running = 1;
    if (pthread_create(&link->tun_thread, NULL, tun_thread, link) != 0) {
        perror("pthread_create");
        link->running = 0;
        if (link->io.tun_fd >= 0) {
            close(link->io.tun_fd);
        }
        free(link);
        return NULL;
    }

    return link;
}

void ppp_link_destroy(ppp_link_t *link)
{
    if (!link) {
        return;
    }
    link->running = 0;
    pthread_join(link->tun_thread, NULL);
    if (link->io.tun_fd >= 0) {
        close(link->io.tun_fd);
    }
    free(link);
}

void ppp_link_set_output(ppp_link_t *link, ppp_link_output_fn fn, void *ctx)
{
    if (!link) {
        return;
    }
    link->io.output = fn;
    link->io.output_ctx = ctx;
}

void ppp_link_input(ppp_link_t *link, const uint8_t *data, size_t len)
{
    (void)link;
    ppp_link_serial_input(data, len);
}

struct ppp_link_io *ppp_link_io(ppp_link_t *link)
{
    return link ? &link->io : NULL;
}
