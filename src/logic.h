// Windows-free helpers, kept separate so they can be unit tested on any OS.
#pragma once

#include <stddef.h>

typedef unsigned short utf16_t;

#define SSID_MAX_BYTES 32
#define PASSPHRASE_MIN 8
#define PASSPHRASE_MAX 63
#define GENERATED_PASSPHRASE_LENGTH 12

typedef enum ValidationResult {
    VALID,
    SSID_EMPTY,
    SSID_TOO_LONG,
    PASSPHRASE_TOO_SHORT,
    PASSPHRASE_TOO_LONG,
    PASSPHRASE_BAD_CHARACTER
} ValidationResult;

// An SSID is limited to 32 bytes of UTF-8, not 32 characters.
ValidationResult validate_ssid(const utf16_t *ssid);

// A WPA2 passphrase is 8 to 63 printable ASCII characters.
ValidationResult validate_passphrase(const utf16_t *passphrase);

// ASCII case-insensitive substring search.
int contains_ignoring_case(const utf16_t *text, const utf16_t *needle);

// Writes a NUL-terminated password using characters that are easy to read out.
// Bytes that would bias the result are skipped. Returns the number of characters
// written, which is less than length if random_bytes ran out.
int generate_passphrase(utf16_t *out, int length, const unsigned char *random_bytes, size_t random_count);
