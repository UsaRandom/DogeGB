#pragma bank 1
#include "sha512_transform.h"
#include "wram_arena.h"
#include <string.h>

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define SHR64(x, n) ((x) >> (n))

#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) ^ (y))))

#define SIGMA0(x) (ROR64(x, 28) ^ ROR64(x, 34) ^ ROR64(x, 39))
#define SIGMA1(x) (ROR64(x, 14) ^ ROR64(x, 18) ^ ROR64(x, 41))
#define sigma0(x) (ROR64(x, 1) ^ ROR64(x, 8) ^ SHR64(x, 7))
#define sigma1(x) (ROR64(x, 19) ^ ROR64(x, 61) ^ SHR64(x, 6))

extern const uint64_t sha512_K[80];

void sha512_transform(SHA512_CTX *ctx, const uint8_t *data) BANKED {
    register uint8_t i;
    /* W[80] lives in the WRAM arena (640 bytes — too large for stack).
       a-h, T1, T2 are 80 bytes total; that is fine on the stack. */
    uint64_t *W = g_arena.w.sha512_W;
    uint64_t a, b, c, d, e, f, g, h, T1, T2;

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

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

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

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}
