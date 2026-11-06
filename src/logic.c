#include "logic.h"

ValidationResult validate_ssid(const utf16_t *ssid)
{
    size_t bytes = 0;

    if (!ssid[0]) {
        return SSID_EMPTY;
    }
    for (const utf16_t *p = ssid; *p; p++) {
        utf16_t c = *p;
        if (c < 0x80) {
            bytes += 1;
        } else if (c < 0x800) {
            bytes += 2;
        } else if (c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {
            bytes += 4;
            p++;
        } else {
            bytes += 3;
        }
    }
    return bytes > SSID_MAX_BYTES ? SSID_TOO_LONG : VALID;
}

ValidationResult validate_passphrase(const utf16_t *passphrase)
{
    int length = 0;

    for (const utf16_t *p = passphrase; *p; p++) {
        if (*p < 0x20 || *p > 0x7E) {
            return PASSPHRASE_BAD_CHARACTER;
        }
        length++;
    }
    if (length < PASSPHRASE_MIN) {
        return PASSPHRASE_TOO_SHORT;
    }
    return length > PASSPHRASE_MAX ? PASSPHRASE_TOO_LONG : VALID;
}

static utf16_t lower(utf16_t c)
{
    return (c >= 'A' && c <= 'Z') ? (utf16_t)(c + ('a' - 'A')) : c;
}

int contains_ignoring_case(const utf16_t *text, const utf16_t *needle)
{
    for (; *text; text++) {
        const utf16_t *t = text;
        const utf16_t *n = needle;
        while (*t && *n && lower(*t) == lower(*n)) {
            t++;
            n++;
        }
        if (!*n) {
            return 1;
        }
    }
    return !needle[0];
}

int generate_passphrase(utf16_t *out, int length, const unsigned char *random_bytes, size_t random_count)
{
    // No 0/O, 1/l/i, so it can be read off a screen and typed on a phone.
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    const unsigned alphabet_size = sizeof(alphabet) - 1;
    const unsigned limit = 256 - (256 % alphabet_size);
    int written = 0;

    for (size_t i = 0; i < random_count && written < length; i++) {
        if (random_bytes[i] < limit) {
            out[written++] = (utf16_t)alphabet[random_bytes[i] % alphabet_size];
        }
    }
    out[written] = 0;
    return written;
}
