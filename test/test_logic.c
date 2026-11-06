#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <uchar.h>

#include "logic.h"

#define U(s) ((const utf16_t *)(u##s))

int main(void)
{
    assert(validate_ssid(U("")) == SSID_EMPTY);
    assert(validate_ssid(U("a")) == VALID);
    assert(validate_ssid(U("12345678901234567890123456789012")) == VALID);
    assert(validate_ssid(U("123456789012345678901234567890123")) == SSID_TOO_LONG);
    // 11 three-byte characters are 33 bytes although only 11 characters.
    assert(validate_ssid(U("日本語日本語日本語日本")) == SSID_TOO_LONG);
    assert(validate_ssid(U("日本語日本語日本語日")) == VALID);
    assert(validate_ssid(U("😀😀😀😀😀😀😀😀")) == VALID);  // 32 bytes
    assert(validate_ssid(U("😀😀😀😀😀😀😀😀a")) == SSID_TOO_LONG);
    assert(validate_ssid(U("é")) == VALID);

    assert(validate_passphrase(U("")) == PASSPHRASE_TOO_SHORT);
    assert(validate_passphrase(U("1234567")) == PASSPHRASE_TOO_SHORT);
    assert(validate_passphrase(U("12345678")) == VALID);
    assert(validate_passphrase(U("with space ~")) == VALID);
    assert(validate_passphrase(U("123456789012345678901234567890123456789012345678901234567890123")) == VALID);
    assert(validate_passphrase(U("1234567890123456789012345678901234567890123456789012345678901234")) == PASSPHRASE_TOO_LONG);
    assert(validate_passphrase(U("pässword1")) == PASSPHRASE_BAD_CHARACTER);
    assert(validate_passphrase(U("tab\there1")) == PASSPHRASE_BAD_CHARACTER);

    assert(contains_ignoring_case(U("Microsoft Wi-Fi Direct Virtual Adapter #2"), U("wi-fi direct")));
    assert(!contains_ignoring_case(U("Intel(R) Wi-Fi 6 AX201"), U("Wi-Fi Direct")));
    assert(!contains_ignoring_case(U(""), U("x")));
    assert(contains_ignoring_case(U("Wi-Fi Direct"), U("Wi-Fi Direct")));
    assert(!contains_ignoring_case(U("Wi-Fi Dir"), U("Wi-Fi Direct")));

    unsigned char bytes[256];
    utf16_t out[300];
    for (int i = 0; i < 256; i++) bytes[i] = (unsigned char)i;
    int counts[128] = {0};
    int written = generate_passphrase(out, 256, bytes, 256);
    assert(written == 248);  // 8 biased values are rejected
    assert(out[248] == 0);
    for (int i = 0; i < written; i++) counts[out[i]]++;
    int used = 0;
    for (int i = 0; i < 128; i++) {
        if (counts[i]) {
            used++;
            assert(counts[i] == 8);  // uniform over the alphabet
            assert(!strchr("01lioIO", i));
        }
    }
    assert(used == 31);
    assert(generate_passphrase(out, 12, bytes, 256) == 12 && out[12] == 0);
    assert(validate_passphrase(out) == VALID);
    assert(generate_passphrase(out, 12, bytes, 5) == 5);  // ran out of random bytes

    puts("logic tests passed");
    return 0;
}
