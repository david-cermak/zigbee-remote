/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "ppp_link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct serial_io serial_io_t;

serial_io_t *serial_io_open(const char *device, int baud, int verbose);
void serial_io_close(serial_io_t *serial);
void serial_io_attach(ppp_link_t *link, serial_io_t *serial);

#ifdef __cplusplus
}
#endif
