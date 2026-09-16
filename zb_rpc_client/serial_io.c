/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for zigbee-remote: detach callbacks during shutdown.
 */
#include "serial_io.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct serial_io {
    int fd;
    pthread_t thread;
    volatile int running;
    int verbose;
    ppp_link_t *link;
};

static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void serial_log_dump(const char *direction, const uint8_t *data, size_t len)
{
    pthread_mutex_lock(&s_log_mutex);
    fprintf(stderr, "serial %s [%zu]:", direction, len);
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, " %02x", data[i]);
    }
    fputc('\n', stderr);
    pthread_mutex_unlock(&s_log_mutex);
}

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B115200;
    }
}

static int serial_set_modem_lines(int fd)
{
    int status = 0;

    if (ioctl(fd, TIOCMGET, &status) != 0) {
        return 0;
    }
    status |= TIOCM_DTR | TIOCM_RTS;
    if (ioctl(fd, TIOCMSET, &status) != 0) {
        return 0;
    }
    return 0;
}

static ssize_t serial_output(void *ctx, const void *data, size_t len)
{
    serial_io_t *serial = (serial_io_t *)ctx;
    ssize_t written = write(serial->fd, data, len);
    if (written < 0) {
        perror("serial write");
        return -1;
    }
    if (serial->verbose && written > 0) {
        serial_log_dump("TX", (const uint8_t *)data, (size_t)written);
    }
    return written;
}

static void *serial_read_task(void *arg)
{
    serial_io_t *serial = (serial_io_t *)arg;
    uint8_t buf[1024];

    while (serial->running) {
        struct pollfd pfd = {
            .fd = serial->fd,
            .events = POLLIN,
        };
        int ready = poll(&pfd, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("serial poll");
            break;
        }
        if (ready == 0) {
            continue;
        }

        ssize_t len = read(serial->fd, buf, sizeof(buf));
        if (len > 0) {
            if (serial->verbose) {
                serial_log_dump("RX", buf, (size_t)len);
            }
            if (serial->link) {
                ppp_link_input(serial->link, buf, (size_t)len);
            }
            continue;
        }
        if (len == 0) {
            continue;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        perror("serial read");
        break;
    }
    return NULL;
}

serial_io_t *serial_io_open(const char *device, int baud, int verbose)
{
    serial_io_t *serial = calloc(1, sizeof(*serial));
    if (!serial) {
        return NULL;
    }

    serial->verbose = verbose;

    serial->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial->fd < 0) {
        perror(device);
        free(serial);
        return NULL;
    }

    struct termios tty;
    if (tcgetattr(serial->fd, &tty) != 0) {
        perror("tcgetattr");
        close(serial->fd);
        free(serial);
        return NULL;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, baud_to_speed(baud));
    cfsetospeed(&tty, baud_to_speed(baud));
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(serial->fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(serial->fd);
        free(serial);
        return NULL;
    }

    serial_set_modem_lines(serial->fd);

    serial->running = 1;
    if (pthread_create(&serial->thread, NULL, serial_read_task, serial) != 0) {
        perror("pthread_create");
        close(serial->fd);
        free(serial);
        return NULL;
    }

    return serial;
}

void serial_io_attach(ppp_link_t *link, serial_io_t *serial)
{
    serial->link = link;
    ppp_link_set_output(link, serial_output, serial);
}

void serial_io_close(serial_io_t *serial)
{
    if (!serial) {
        return;
    }
    serial->running = 0;
    pthread_join(serial->thread, NULL);
    if (serial->link) {
        ppp_link_set_output(serial->link, NULL, NULL);
        serial->link = NULL;
    }
    close(serial->fd);
    free(serial);
}
