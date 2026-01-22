#pragma bank 2
#include "sha512.h"
#include "sha512_transform.h"
#include <string.h>

// SHA-512 implementation for GBDK
// Note: 64-bit operations will be slow on 8-bit CPU

void sha512_init(SHA512_CTX *ctx) BANKED {
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
}

void sha512_update(SHA512_CTX *ctx, const uint8_t *data, uint32_t len) BANKED {
    uint32_t i, index, partLen;

    index = (uint32_t)((ctx->count[0] >> 3) & 0x7F);

    if ((ctx->count[0] += ((uint64_t)len << 3)) < ((uint64_t)len << 3))
        ctx->count[1]++;
    ctx->count[1] += ((uint64_t)len >> 29);

    partLen = 128 - index;

    if (len >= partLen) {
        memcpy(&ctx->buffer[index], data, partLen);
        sha512_transform(ctx, ctx->buffer);

        for (i = partLen; i + 127 < len; i += 128)
            sha512_transform(ctx, &data[i]);

        index = 0;
    } else {
        i = 0;
    }

    memcpy(&ctx->buffer[index], &data[i], len - i);
}

void sha512_final(SHA512_CTX *ctx, uint8_t *digest) BANKED {
    uint8_t bits[16];
    uint32_t index, padLen;
    uint8_t i;

    // Save bit count (big-endian)
    for (i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count[1] >> (56 - i*8)) & 0xFF);
        bits[i+8] = (uint8_t)((ctx->count[0] >> (56 - i*8)) & 0xFF);
    }

    // Pad
    index = (uint32_t)((ctx->count[0] >> 3) & 0x7F);
    padLen = (index < 112) ? (112 - index) : (240 - index);
    
    uint8_t padding[128];
    padding[0] = 0x80;
    for (i = 1; i < 128; i++) padding[i] = 0;
    
    sha512_update(ctx, padding, padLen);
    sha512_update(ctx, bits, 16);

    // Output (big-endian)
    for (i = 0; i < 8; i++) {
        digest[i*8] = (uint8_t)((ctx->state[i] >> 56) & 0xFF);
        digest[i*8+1] = (uint8_t)((ctx->state[i] >> 48) & 0xFF);
        digest[i*8+2] = (uint8_t)((ctx->state[i] >> 40) & 0xFF);
        digest[i*8+3] = (uint8_t)((ctx->state[i] >> 32) & 0xFF);
        digest[i*8+4] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i*8+5] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i*8+6] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i*8+7] = (uint8_t)(ctx->state[i] & 0xFF);
    }
}
