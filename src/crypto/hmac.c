#pragma bank 4
#include "hmac.h"
#include "sha512.h"
#include <string.h>

// HMAC-SHA512 implementation

// Static buffers to avoid stack overflow
static SHA512_CTX ctx;
static uint8_t k_pad[128];
static uint8_t tk[SHA512_HASH_LENGTH];

void hmac_sha512(uint8_t *out, const uint8_t *key, uint32_t key_len, const uint8_t *data, uint32_t data_len) BANKED {
    uint8_t i;

    // If key is longer than 128 bytes, hash it first
    if (key_len > 128) {
        sha512_init(&ctx);
        sha512_update(&ctx, key, key_len);
        sha512_final(&ctx, tk);
        key = tk;
        key_len = SHA512_HASH_LENGTH;
    }

    // Prepare inner padding
    memset(k_pad, 0x36, 128);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    // Inner hash: H(K XOR ipad, text)
    sha512_init(&ctx);
    sha512_update(&ctx, k_pad, 128);
    sha512_update(&ctx, data, data_len);
    sha512_final(&ctx, out);

    // Prepare outer padding
    memset(k_pad, 0x5c, 128);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    // Outer hash: H(K XOR opad, inner_hash)
    sha512_init(&ctx);
    sha512_update(&ctx, k_pad, 128);
    sha512_update(&ctx, out, SHA512_HASH_LENGTH);
    sha512_final(&ctx, out);
}
