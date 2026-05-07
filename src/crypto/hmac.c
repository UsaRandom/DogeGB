#pragma bank 4
#include "hmac.h"
#include "sha512.h"
#include "wram_arena.h"
#include <string.h>

#define ctx    g_arena.w.hmac_ctx
#define k_pad  g_arena.w.hmac_k_pad
#define tk     g_arena.w.hmac_tk

void hmac_sha512(uint8_t *out, const uint8_t *key, uint32_t key_len, const uint8_t *data, uint32_t data_len) BANKED {
    uint8_t i;

    if (key_len > 128) {
        sha512_init(&ctx);
        sha512_update(&ctx, key, key_len);
        sha512_final(&ctx, tk);
        key = tk;
        key_len = SHA512_HASH_LENGTH;
    }

    memset(k_pad, 0x36, 128);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    sha512_init(&ctx);
    sha512_update(&ctx, k_pad, 128);
    sha512_update(&ctx, data, data_len);
    sha512_final(&ctx, out);

    memset(k_pad, 0x5c, 128);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    sha512_init(&ctx);
    sha512_update(&ctx, k_pad, 128);
    sha512_update(&ctx, out, SHA512_HASH_LENGTH);
    sha512_final(&ctx, out);
}

#undef ctx
#undef k_pad
#undef tk
