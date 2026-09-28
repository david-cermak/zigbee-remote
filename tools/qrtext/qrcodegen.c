/*
 * qrtext - print a QR Code as text in the terminal.
 *
 * Build:   gcc qrcodegen.c -o qrcodegen
 * Run:     ./qrcodegen example.com
 *          ./qrcodegen --compact example.com
 */

#include <stdio.h>
#include <string.h>

#include "qrcodegen.h"

static int print_compact(const char *text)
{
    static char qr[QRCODEGEN_TEXT_COMPACT_MAX_LEN];
    if (qrcodegen_c(text, qr) == NULL) {
        return 0;
    }
    fputs(qr, stdout);
    return 1;
}

static int print_full(const char *text)
{
    static char qr[QRCODEGEN_TEXT_MAX_LEN];
    if (qrcodegen(text, qr) == NULL) {
        return 0;
    }
    fputs(qr, stdout);
    return 1;
}

int main(int argc, char *argv[])
{
    int compact = 0;
    const char *text = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--compact") == 0 || strcmp(argv[i], "-c") == 0) {
            compact = 1;
        } else if (text == NULL) {
            text = argv[i];
        } else {
            text = NULL;
            break;
        }
    }

    if (text == NULL) {
        fprintf(stderr, "usage: %s [--compact|-c] <text>\n", argv[0]);
        return 1;
    }

    if (compact ? print_compact(text) : print_full(text)) {
        return 0;
    }
    fprintf(stderr, "error: text is too long to encode\n");
    return 1;
}
