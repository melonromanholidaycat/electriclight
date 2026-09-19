#include "el_base64.h"

static int value_of(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int el_base64_decode(const char *in, uint8_t *out, size_t out_max)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;

    for (const char *p = in; *p; p++) {
        // Padding ends the data. Anything after it is ignored rather than
        // treated as an error, which is what the encoder's own reader does.
        if (*p == '=') break;
        const int v = value_of(*p);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits < 8) continue;
        bits -= 8;
        if (n >= out_max) return -1;
        out[n++] = (uint8_t)((acc >> bits) & 0xff);
    }
    return (int)n;
}
