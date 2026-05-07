#pragma bank 3
#include "pbkdf2.h"
#include "sha512.h"
#include <string.h>
#include <stdio.h>
#include "progress.h"

extern uint8_t crypto_debug;

// Static buffers to avoid stack overflow
static uint8_t u[SHA512_HASH_LENGTH];
static uint8_t k_ipad[128];
static uint8_t k_opad[128];
static uint8_t tk[SHA512_HASH_LENGTH];
// Only store the 64-byte state snapshots, not full SHA512_CTX (saves 288 bytes vs 2×SHA512_CTX)
static uint8_t inner_state_init[64];
static uint8_t outer_state_init[64];
static SHA512_CTX ctx_inner, ctx_outer;
static uint8_t salt_block[132];

void pbkdf2_hmac_sha512(uint8_t *out, const uint8_t *password, uint32_t password_len, const uint8_t *salt, uint32_t salt_len, uint32_t iterations) BANKED {
    register uint32_t i;

    // Prepare key (password)
    const uint8_t *key = password;
    uint32_t key_len = password_len;

    if (key_len > 128) {
        sha512_init(&ctx_inner);
        sha512_update(&ctx_inner, key, key_len);
        sha512_final(&ctx_inner, tk);
        key = tk;
        key_len = SHA512_HASH_LENGTH;
    }

    // Precompute inner padding
    memset(k_ipad, 0x36, 128);
    for (i = 0; i < key_len; i++) k_ipad[i] ^= key[i];

    // Precompute outer padding
    memset(k_opad, 0x5c, 128);
    for (i = 0; i < key_len; i++) k_opad[i] ^= key[i];

    // Capture inner state after processing k_ipad (reuse ctx_inner as temporary)
    sha512_init(&ctx_inner);
    sha512_update(&ctx_inner, k_ipad, 128);
    memcpy(inner_state_init, &ctx_inner.state, 64);

    // Capture outer state after processing k_opad (reuse ctx_outer as temporary)
    sha512_init(&ctx_outer);
    sha512_update(&ctx_outer, k_opad, 128);
    memcpy(outer_state_init, &ctx_outer.state, 64);

    // Block 1 (BIP39 only needs 1 block of 64 bytes output)
    memcpy(salt_block, salt, salt_len);
    salt_block[salt_len] = 0;
    salt_block[salt_len+1] = 0;
    salt_block[salt_len+2] = 0;
    salt_block[salt_len+3] = 1;

    // U1 = PRF(P, S || 1)
    memcpy(&ctx_inner.state, inner_state_init, 64);
    ctx_inner.count[0] = 1024; // 128 bytes * 8 bits
    ctx_inner.count[1] = 0;
    memset(ctx_inner.buffer, 0, 128);
    sha512_update(&ctx_inner, salt_block, salt_len + 4);
    sha512_final(&ctx_inner, u);
    // Outer hash
    memcpy(&ctx_outer.state, outer_state_init, 64);
    ctx_outer.count[0] = 1024;
    ctx_outer.count[1] = 0;
    memset(ctx_outer.buffer, 0, 128);
    sha512_update(&ctx_outer, u, SHA512_HASH_LENGTH);
    sha512_final(&ctx_outer, u);

    // Copy U1 to result
    memcpy(out, u, SHA512_HASH_LENGTH);

    add_progress(WEIGHT_PBKDF2);

    // Loop for remaining iterations
    for (i = 1; i < iterations; i++) {
        // Inner hash
        memcpy(&ctx_inner.state, inner_state_init, 64);
        ctx_inner.count[0] = 1024;
        ctx_inner.count[1] = 0;
        memset(ctx_inner.buffer, 0, 128);
        sha512_update(&ctx_inner, u, SHA512_HASH_LENGTH);
        sha512_final(&ctx_inner, u);

        // Outer hash
        memcpy(&ctx_outer.state, outer_state_init, 64);
        ctx_outer.count[0] = 1024;
        ctx_outer.count[1] = 0;
        memset(ctx_outer.buffer, 0, 128);
        sha512_update(&ctx_outer, u, SHA512_HASH_LENGTH);
        sha512_final(&ctx_outer, u);

        // XOR into result - FULLY unrolled 64-byte XOR (no loop)
        out[0] ^= u[0]; out[1] ^= u[1]; out[2] ^= u[2]; out[3] ^= u[3];
        out[4] ^= u[4]; out[5] ^= u[5]; out[6] ^= u[6]; out[7] ^= u[7];
        out[8] ^= u[8]; out[9] ^= u[9]; out[10] ^= u[10]; out[11] ^= u[11];
        out[12] ^= u[12]; out[13] ^= u[13]; out[14] ^= u[14]; out[15] ^= u[15];
        out[16] ^= u[16]; out[17] ^= u[17]; out[18] ^= u[18]; out[19] ^= u[19];
        out[20] ^= u[20]; out[21] ^= u[21]; out[22] ^= u[22]; out[23] ^= u[23];
        out[24] ^= u[24]; out[25] ^= u[25]; out[26] ^= u[26]; out[27] ^= u[27];
        out[28] ^= u[28]; out[29] ^= u[29]; out[30] ^= u[30]; out[31] ^= u[31];
        out[32] ^= u[32]; out[33] ^= u[33]; out[34] ^= u[34]; out[35] ^= u[35];
        out[36] ^= u[36]; out[37] ^= u[37]; out[38] ^= u[38]; out[39] ^= u[39];
        out[40] ^= u[40]; out[41] ^= u[41]; out[42] ^= u[42]; out[43] ^= u[43];
        out[44] ^= u[44]; out[45] ^= u[45]; out[46] ^= u[46]; out[47] ^= u[47];
        out[48] ^= u[48]; out[49] ^= u[49]; out[50] ^= u[50]; out[51] ^= u[51];
        out[52] ^= u[52]; out[53] ^= u[53]; out[54] ^= u[54]; out[55] ^= u[55];
        out[56] ^= u[56]; out[57] ^= u[57]; out[58] ^= u[58]; out[59] ^= u[59];
        out[60] ^= u[60]; out[61] ^= u[61]; out[62] ^= u[62]; out[63] ^= u[63];

        add_progress(WEIGHT_PBKDF2);
    }
}
