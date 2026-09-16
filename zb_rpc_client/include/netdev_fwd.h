/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void netdev_fwd_configure(const char *host, uint16_t port);
int netdev_fwd_enabled(void);
void netdev_fwd_on_link_up(void);

#ifdef __cplusplus
}
#endif
