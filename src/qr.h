// Minimal QR code encoder, kept free of Windows so it can be tested on any OS.
// Byte mode, error correction level M, versions 1 to 10 (up to 213 bytes).
#pragma once

#define QR_MAX_SIZE 57 // modules per side at version 10

// Encodes data into modules, a size x size grid in row-major order where 1 is
// dark. modules must hold QR_MAX_SIZE * QR_MAX_SIZE bytes. Returns 1 on success
// and 0 if the data does not fit. Not thread safe: it uses static work space so
// that no large buffers sit on the stack.
int qr_encode(const unsigned char *data, int length, unsigned char *modules, int *size);
