#include "qr.h"

#define MAX_VERSION 10
#define MAX_BLOCKS 5
#define MAX_BLOCK_LENGTH 70
#define MAX_CODEWORDS 346
#define MAX_ECC 26

// Error correction level M, indexed by version.
static const int ECC_PER_BLOCK[MAX_VERSION + 1] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
static const int BLOCK_COUNT[MAX_VERSION + 1] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};

static unsigned char g_function[QR_MAX_SIZE * QR_MAX_SIZE];

static int g_size;
static unsigned char *g_modules;

static int raw_codewords(int version)
{
    int bits = (16 * version + 128) * version + 64;
    if (version >= 2) {
        int align = version / 7 + 2;
        bits -= (25 * align - 10) * align - 55;
        if (version >= 7) {
            bits -= 36;
        }
    }
    return bits / 8;
}

static int data_codewords(int version)
{
    return raw_codewords(version) - ECC_PER_BLOCK[version] * BLOCK_COUNT[version];
}

static int get(int x, int y)
{
    return g_modules[y * g_size + x];
}

static void set_function(int x, int y, int dark)
{
    g_modules[y * g_size + x] = (unsigned char)dark;
    g_function[y * g_size + x] = 1;
}

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static void draw_finder(int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int distance = max_int(abs_int(dx), abs_int(dy));
            int x = cx + dx;
            int y = cy + dy;
            if (x >= 0 && x < g_size && y >= 0 && y < g_size) {
                set_function(x, y, distance != 2 && distance != 4);
            }
        }
    }
}

static void draw_alignment(int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            set_function(cx + dx, cy + dy, max_int(abs_int(dx), abs_int(dy)) != 1);
        }
    }
}

static void draw_format(int mask)
{
    // Level M is 0 in the format bits.
    int data = mask;
    int remainder = data;
    for (int i = 0; i < 10; i++) {
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    }
    int bits = ((data << 10) | remainder) ^ 0x5412;

    for (int i = 0; i <= 5; i++) {
        set_function(8, i, (bits >> i) & 1);
    }
    set_function(8, 7, (bits >> 6) & 1);
    set_function(8, 8, (bits >> 7) & 1);
    set_function(7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) {
        set_function(14 - i, 8, (bits >> i) & 1);
    }
    for (int i = 0; i < 8; i++) {
        set_function(g_size - 1 - i, 8, (bits >> i) & 1);
    }
    for (int i = 8; i < 15; i++) {
        set_function(8, g_size - 15 + i, (bits >> i) & 1);
    }
    set_function(8, g_size - 8, 1);
}

static void draw_version(int version)
{
    if (version < 7) {
        return;
    }
    int remainder = version;
    for (int i = 0; i < 12; i++) {
        remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
    }
    int bits = (version << 12) | remainder;
    for (int i = 0; i < 18; i++) {
        int bit = (bits >> i) & 1;
        int a = g_size - 11 + i % 3;
        int b = i / 3;
        set_function(a, b, bit);
        set_function(b, a, bit);
    }
}

static void draw_function_patterns(int version)
{
    for (int i = 0; i < g_size; i++) {
        set_function(6, i, i % 2 == 0);
        set_function(i, 6, i % 2 == 0);
    }
    draw_finder(3, 3);
    draw_finder(g_size - 4, 3);
    draw_finder(3, g_size - 4);

    if (version >= 2) {
        int count = version / 7 + 2;
        int step = (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
        int positions[4];
        positions[0] = 6;
        for (int i = count - 1, position = g_size - 7; i >= 1; i--, position -= step) {
            positions[i] = position;
        }
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < count; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0)) {
                    continue;
                }
                draw_alignment(positions[i], positions[j]);
            }
        }
    }

    draw_format(0); // reserves the area; rewritten once the mask is chosen
    draw_version(version);
}

static unsigned char gf_multiply(unsigned char x, unsigned char y)
{
    int product = 0;
    for (int i = 7; i >= 0; i--) {
        product = (product << 1) ^ ((product >> 7) * 0x11D);
        product ^= ((y >> i) & 1) * x;
    }
    return (unsigned char)product;
}

static void reed_solomon(const unsigned char *data, int length, int degree, unsigned char *result)
{
    unsigned char divisor[MAX_ECC];

    for (int i = 0; i < degree; i++) {
        divisor[i] = 0;
        result[i] = 0;
    }
    divisor[degree - 1] = 1;
    unsigned char root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            divisor[j] = gf_multiply(divisor[j], root);
            if (j + 1 < degree) {
                divisor[j] ^= divisor[j + 1];
            }
        }
        root = gf_multiply(root, 2);
    }

    for (int i = 0; i < length; i++) {
        unsigned char factor = data[i] ^ result[0];
        for (int j = 0; j + 1 < degree; j++) {
            result[j] = result[j + 1];
        }
        result[degree - 1] = 0;
        for (int j = 0; j < degree; j++) {
            result[j] ^= gf_multiply(divisor[j], factor);
        }
    }
}

// Splits the data into blocks, adds error correction to each and interleaves them.
static void add_ecc_and_interleave(int version, const unsigned char *data, unsigned char *out)
{
    int blocks = BLOCK_COUNT[version];
    int ecc_length = ECC_PER_BLOCK[version];
    int raw = raw_codewords(version);
    int short_blocks = blocks - raw % blocks;
    int short_length = raw / blocks;
    unsigned char block_data[MAX_BLOCKS][MAX_BLOCK_LENGTH];
    unsigned char block_ecc[MAX_BLOCKS][MAX_ECC];
    int data_lengths[MAX_BLOCKS];
    int k = 0;

    for (int i = 0; i < blocks; i++) {
        int length = short_length - ecc_length + (i < short_blocks ? 0 : 1);
        for (int j = 0; j < length; j++) {
            block_data[i][j] = data[k++];
        }
        data_lengths[i] = length;
        reed_solomon(block_data[i], length, ecc_length, block_ecc[i]);
    }

    int n = 0;
    for (int i = 0; i < short_length - ecc_length + 1; i++) {
        for (int j = 0; j < blocks; j++) {
            if (i < data_lengths[j]) {
                out[n++] = block_data[j][i];
            }
        }
    }
    for (int i = 0; i < ecc_length; i++) {
        for (int j = 0; j < blocks; j++) {
            out[n++] = block_ecc[j][i];
        }
    }
}

static void draw_codewords(const unsigned char *codewords, int count)
{
    int bit = 0;
    int total_bits = count * 8;

    for (int right = g_size - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (int vertical = 0; vertical < g_size; vertical++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? g_size - 1 - vertical : vertical;
                if (!g_function[y * g_size + x] && bit < total_bits) {
                    g_modules[y * g_size + x] = (codewords[bit >> 3] >> (7 - (bit & 7))) & 1;
                    bit++;
                }
            }
        }
    }
}

static int mask_applies(int mask, int x, int y)
{
    switch (mask) {
    case 0:
        return (x + y) % 2 == 0;
    case 1:
        return y % 2 == 0;
    case 2:
        return x % 3 == 0;
    case 3:
        return (x + y) % 3 == 0;
    case 4:
        return (x / 3 + y / 2) % 2 == 0;
    case 5:
        return x * y % 2 + x * y % 3 == 0;
    case 6:
        return (x * y % 2 + x * y % 3) % 2 == 0;
    default:
        return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

// Applying the same mask twice undoes it.
static void apply_mask(int mask)
{
    for (int y = 0; y < g_size; y++) {
        for (int x = 0; x < g_size; x++) {
            if (!g_function[y * g_size + x] && mask_applies(mask, x, y)) {
                g_modules[y * g_size + x] ^= 1;
            }
        }
    }
}

static int finder_like(int line, int index, int horizontal)
{
    static const unsigned char pattern[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    int forward = 1;
    int backward = 1;

    for (int i = 0; i < 11; i++) {
        int x = horizontal ? index + i : line;
        int y = horizontal ? line : index + i;
        int value = get(x, y);
        if (value != pattern[i]) {
            forward = 0;
        }
        if (value != pattern[10 - i]) {
            backward = 0;
        }
    }
    return forward + backward;
}

static int penalty(void)
{
    int result = 0;
    int dark = 0;

    for (int line = 0; line < g_size; line++) {
        for (int direction = 0; direction < 2; direction++) {
            int run = 1;
            for (int i = 1; i <= g_size; i++) {
                int same = 0;
                if (i < g_size) {
                    int a = direction ? get(line, i - 1) : get(i - 1, line);
                    int b = direction ? get(line, i) : get(i, line);
                    same = a == b;
                }
                if (same) {
                    run++;
                } else {
                    if (run >= 5) {
                        result += run - 2;
                    }
                    run = 1;
                }
            }
            for (int i = 0; i + 11 <= g_size; i++) {
                result += 40 * finder_like(line, i, !direction);
            }
        }
    }

    for (int y = 0; y < g_size; y++) {
        for (int x = 0; x < g_size; x++) {
            int value = get(x, y);
            dark += value;
            if (x + 1 < g_size && y + 1 < g_size && get(x + 1, y) == value && get(x, y + 1) == value &&
                get(x + 1, y + 1) == value) {
                result += 3;
            }
        }
    }

    int total = g_size * g_size;
    int k = (abs_int(dark * 20 - total * 10) + total - 1) / total - 1;
    return result + (k > 0 ? k : 0) * 10;
}

int qr_encode(const unsigned char *data, int length, unsigned char *modules, int *size)
{
    unsigned char payload[MAX_CODEWORDS];
    unsigned char codewords[MAX_CODEWORDS];
    int version = 0;

    if (length < 0) {
        return 0;
    }
    for (int v = 1; v <= MAX_VERSION; v++) {
        int count_bits = v < 10 ? 8 : 16;
        if (4 + count_bits + length * 8 <= data_codewords(v) * 8) {
            version = v;
            break;
        }
    }
    if (!version) {
        return 0;
    }

    int capacity = data_codewords(version);
    int bits = 0;
    for (int i = 0; i < capacity; i++) {
        payload[i] = 0;
    }
#define PUT_BITS(value, count)                                                                     \
    for (int b = (count) - 1; b >= 0; b--, bits++) {                                               \
        payload[bits >> 3] |= (unsigned char)((((value) >> b) & 1) << (7 - (bits & 7)));          \
    }
    PUT_BITS(4, 4);
    PUT_BITS(length, version < 10 ? 8 : 16);
    for (int i = 0; i < length; i++) {
        PUT_BITS(data[i], 8);
    }
#undef PUT_BITS
    // The terminator and the pad to a byte boundary are already zero bits.
    for (int i = (bits + 7) / 8, pad = 0xEC; i < capacity; i++, pad ^= 0xEC ^ 0x11) {
        payload[i] = (unsigned char)pad;
    }

    g_size = version * 4 + 17;
    g_modules = modules;
    for (int i = 0; i < g_size * g_size; i++) {
        modules[i] = 0;
        g_function[i] = 0;
    }
    draw_function_patterns(version);
    add_ecc_and_interleave(version, payload, codewords);
    draw_codewords(codewords, raw_codewords(version));

    int best_mask = 0;
    int best_penalty = 0x7FFFFFFF;
    for (int mask = 0; mask < 8; mask++) {
        apply_mask(mask);
        draw_format(mask);
        int score = penalty();
        if (score < best_penalty) {
            best_penalty = score;
            best_mask = mask;
        }
        apply_mask(mask);
    }
    apply_mask(best_mask);
    draw_format(best_mask);

    *size = g_size;
    return 1;
}
