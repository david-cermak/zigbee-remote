/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ppp_link ppp_link_t;

typedef enum {
    PPP_LINK_MODE_SERVER = 0,
    PPP_LINK_MODE_CLIENT = 1,
} ppp_link_mode_t;

typedef struct {
    ppp_link_mode_t mode;
    const char *local_ip;
    const char *peer_ip;
    const char *dns_ip;
    int ipv6;
    int netdev; /* keep IP in lwIP and TCP-forward (--netdev) */
} ppp_config_t;

typedef struct {
    const char *tun_dev;
    const char *tun_if;
    const ppp_config_t *ppp;
} ppp_link_config_t;

typedef ssize_t (*ppp_link_output_fn)(void *ctx, const void *data, size_t len);

struct ppp_link_io {
    int tun_fd;
    uint8_t *in_buf;
    uint8_t *out_buf;
    const ppp_config_t *ppp_cfg;
    ppp_link_output_fn output;
    void *output_ctx;
};

ppp_link_t *ppp_link_create(const ppp_link_config_t *config);
void ppp_link_destroy(ppp_link_t *link);
void ppp_link_set_output(ppp_link_t *link, ppp_link_output_fn fn, void *ctx);
void ppp_link_input(ppp_link_t *link, const uint8_t *data, size_t len);
struct ppp_link_io *ppp_link_io(ppp_link_t *link);
int ppp_link_lwip_init(ppp_link_t *link);
int ppp_link_tun_poll(ppp_link_t *link);
void ppp_link_serial_input(const uint8_t *data, size_t len);
int ppp_link_is_up(void);

#ifdef __cplusplus
}
#endif
