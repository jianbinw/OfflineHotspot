// Reads bytes from stdin and prints the QR code as rows of 0 and 1. Used by test/test_qr.py.
#include <stdio.h>
#include "qr.h"

int main(void)
{
    static unsigned char input[1024];
    static unsigned char modules[QR_MAX_SIZE * QR_MAX_SIZE];
    int size = 0;
    int length = (int)fread(input, 1, sizeof(input), stdin);

    if (!qr_encode(input, length, modules, &size)) {
        puts("too long");
        return 1;
    }
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            putchar('0' + modules[y * size + x]);
        }
        putchar('\n');
    }
    return 0;
}
