#pragma bank 1
#include "sha512_transform.h"
#include <string.h>

// SHA-512 transform function split for banking

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define SHR64(x, n) ((x) >> (n))

// Optimized CH: z ^ (x & (y ^ z)) - 3 ops instead of 4
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))

// Optimized MAJ: (x & y) | (z & (x ^ y)) - 4 ops instead of 6
#define MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) ^ (y))))

#define SIGMA0(x) (ROR64(x, 28) ^ ROR64(x, 34) ^ ROR64(x, 39))
#define SIGMA1(x) (ROR64(x, 14) ^ ROR64(x, 18) ^ ROR64(x, 41))
#define sigma0(x) (ROR64(x, 1) ^ ROR64(x, 8) ^ SHR64(x, 7))
#define sigma1(x) (ROR64(x, 19) ^ ROR64(x, 61) ^ SHR64(x, 6))

// External constant table (in separate file for banking)
extern const uint64_t sha512_K[80];

// Static buffer to avoid stack overflow (640 bytes is too big for stack!)
static uint64_t W[80];
static uint64_t a, b, c, d, e, f, g, h;
static uint64_t T1, T2;

void sha512_transform(SHA512_CTX *ctx, const uint8_t *data) BANKED {
    register uint8_t i;

    // Prepare message schedule
    for (i = 0; i < 16; i++) {
        W[i] = ((uint64_t)data[i*8] << 56) |
               ((uint64_t)data[i*8+1] << 48) |
               ((uint64_t)data[i*8+2] << 40) |
               ((uint64_t)data[i*8+3] << 32) |
               ((uint64_t)data[i*8+4] << 24) |
               ((uint64_t)data[i*8+5] << 16) |
               ((uint64_t)data[i*8+6] << 8) |
               ((uint64_t)data[i*8+7]);
    }

    for (i = 16; i < 80; i++) {
        W[i] = sigma1(W[i-2]) + W[i-7] + sigma0(W[i-15]) + W[i-16];
    }

    // Initialize working variables
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    // Main loop with pointer arithmetic
    uint64_t *w_ptr = W;
    uint64_t *k_ptr = sha512_K;
    for (i = 0; i < 80; i++) {
        T1 = h + SIGMA1(e) + CH(e, f, g) + *k_ptr + *w_ptr;
        T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
        k_ptr++;
        w_ptr++;
    }

    // Add to state
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}
