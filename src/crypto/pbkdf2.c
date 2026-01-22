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
static SHA512_CTX ctx_inner_init, ctx_outer_init;
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
    
    // Precompute inner state (after processing k_ipad)
    sha512_init(&ctx_inner_init);
    sha512_update(&ctx_inner_init, k_ipad, 128);
    
    // Precompute outer state (after processing k_opad)
    sha512_init(&ctx_outer_init);
    sha512_update(&ctx_outer_init, k_opad, 128);
    
    // Block 1 (BIP39 only needs 1 block of 64 bytes output)
    memcpy(salt_block, salt, salt_len);
    salt_block[salt_len] = 0;
    salt_block[salt_len+1] = 0;
    salt_block[salt_len+2] = 0;
    salt_block[salt_len+3] = 1;
    
    // U1 = PRF(P, S || 1)
    memcpy(ctx_inner.state, ctx_inner_init.state, 64); // 8*uint64_t
    ctx_inner.count[0] = 1024; // 128 bytes * 8 bits
    ctx_inner.count[1] = 0;
    memset(ctx_inner.buffer, 0, 128);
    sha512_update(&ctx_inner, salt_block, salt_len + 4);
    sha512_final(&ctx_inner, u);
    // Outer hash
    memcpy(ctx_outer.state, ctx_outer_init.state, 64);
    ctx_outer.count[0] = 1024;
    ctx_outer.count[1] = 0;
    memset(ctx_outer.buffer, 0, 128);
    sha512_update(&ctx_outer, u, SHA512_HASH_LENGTH);
    sha512_final(&ctx_outer, u);
    
    // Copy U1 to result
    memcpy(out, u, SHA512_HASH_LENGTH);

    
    add_progress(WEIGHT_PBKDF2);
    // Loop for remaining iterations
    i = 1;
    for (; i < iterations; i++) {
        // U_i = PRF(P, U_{i-1})
        
    // Inner hash - manual ctx copy (64 bytes instead of 208)
    // Unrolled state copy (8*8 bytes)
    ((uint8_t*)&ctx_inner.state)[0] = ((uint8_t*)&ctx_inner_init.state)[0];
    ((uint8_t*)&ctx_inner.state)[1] = ((uint8_t*)&ctx_inner_init.state)[1];
    ((uint8_t*)&ctx_inner.state)[2] = ((uint8_t*)&ctx_inner_init.state)[2];
    ((uint8_t*)&ctx_inner.state)[3] = ((uint8_t*)&ctx_inner_init.state)[3];
    ((uint8_t*)&ctx_inner.state)[4] = ((uint8_t*)&ctx_inner_init.state)[4];
    ((uint8_t*)&ctx_inner.state)[5] = ((uint8_t*)&ctx_inner_init.state)[5];
    ((uint8_t*)&ctx_inner.state)[6] = ((uint8_t*)&ctx_inner_init.state)[6];
    ((uint8_t*)&ctx_inner.state)[7] = ((uint8_t*)&ctx_inner_init.state)[7];
    ((uint8_t*)&ctx_inner.state)[8] = ((uint8_t*)&ctx_inner_init.state)[8];
    ((uint8_t*)&ctx_inner.state)[9] = ((uint8_t*)&ctx_inner_init.state)[9];
    ((uint8_t*)&ctx_inner.state)[10] = ((uint8_t*)&ctx_inner_init.state)[10];
    ((uint8_t*)&ctx_inner.state)[11] = ((uint8_t*)&ctx_inner_init.state)[11];
    ((uint8_t*)&ctx_inner.state)[12] = ((uint8_t*)&ctx_inner_init.state)[12];
    ((uint8_t*)&ctx_inner.state)[13] = ((uint8_t*)&ctx_inner_init.state)[13];
    ((uint8_t*)&ctx_inner.state)[14] = ((uint8_t*)&ctx_inner_init.state)[14];
    ((uint8_t*)&ctx_inner.state)[15] = ((uint8_t*)&ctx_inner_init.state)[15];
    ((uint8_t*)&ctx_inner.state)[16] = ((uint8_t*)&ctx_inner_init.state)[16];
    ((uint8_t*)&ctx_inner.state)[17] = ((uint8_t*)&ctx_inner_init.state)[17];
    ((uint8_t*)&ctx_inner.state)[18] = ((uint8_t*)&ctx_inner_init.state)[18];
    ((uint8_t*)&ctx_inner.state)[19] = ((uint8_t*)&ctx_inner_init.state)[19];
    ((uint8_t*)&ctx_inner.state)[20] = ((uint8_t*)&ctx_inner_init.state)[20];
    ((uint8_t*)&ctx_inner.state)[21] = ((uint8_t*)&ctx_inner_init.state)[21];
    ((uint8_t*)&ctx_inner.state)[22] = ((uint8_t*)&ctx_inner_init.state)[22];
    ((uint8_t*)&ctx_inner.state)[23] = ((uint8_t*)&ctx_inner_init.state)[23];
    ((uint8_t*)&ctx_inner.state)[24] = ((uint8_t*)&ctx_inner_init.state)[24];
    ((uint8_t*)&ctx_inner.state)[25] = ((uint8_t*)&ctx_inner_init.state)[25];
    ((uint8_t*)&ctx_inner.state)[26] = ((uint8_t*)&ctx_inner_init.state)[26];
    ((uint8_t*)&ctx_inner.state)[27] = ((uint8_t*)&ctx_inner_init.state)[27];
    ((uint8_t*)&ctx_inner.state)[28] = ((uint8_t*)&ctx_inner_init.state)[28];
    ((uint8_t*)&ctx_inner.state)[29] = ((uint8_t*)&ctx_inner_init.state)[29];
    ((uint8_t*)&ctx_inner.state)[30] = ((uint8_t*)&ctx_inner_init.state)[30];
    ((uint8_t*)&ctx_inner.state)[31] = ((uint8_t*)&ctx_inner_init.state)[31];
    ((uint8_t*)&ctx_inner.state)[32] = ((uint8_t*)&ctx_inner_init.state)[32];
    ((uint8_t*)&ctx_inner.state)[33] = ((uint8_t*)&ctx_inner_init.state)[33];
    ((uint8_t*)&ctx_inner.state)[34] = ((uint8_t*)&ctx_inner_init.state)[34];
    ((uint8_t*)&ctx_inner.state)[35] = ((uint8_t*)&ctx_inner_init.state)[35];
    ((uint8_t*)&ctx_inner.state)[36] = ((uint8_t*)&ctx_inner_init.state)[36];
    ((uint8_t*)&ctx_inner.state)[37] = ((uint8_t*)&ctx_inner_init.state)[37];
    ((uint8_t*)&ctx_inner.state)[38] = ((uint8_t*)&ctx_inner_init.state)[38];
    ((uint8_t*)&ctx_inner.state)[39] = ((uint8_t*)&ctx_inner_init.state)[39];
    ((uint8_t*)&ctx_inner.state)[40] = ((uint8_t*)&ctx_inner_init.state)[40];
    ((uint8_t*)&ctx_inner.state)[41] = ((uint8_t*)&ctx_inner_init.state)[41];
    ((uint8_t*)&ctx_inner.state)[42] = ((uint8_t*)&ctx_inner_init.state)[42];
    ((uint8_t*)&ctx_inner.state)[43] = ((uint8_t*)&ctx_inner_init.state)[43];
    ((uint8_t*)&ctx_inner.state)[44] = ((uint8_t*)&ctx_inner_init.state)[44];
    ((uint8_t*)&ctx_inner.state)[45] = ((uint8_t*)&ctx_inner_init.state)[45];
    ((uint8_t*)&ctx_inner.state)[46] = ((uint8_t*)&ctx_inner_init.state)[46];
    ((uint8_t*)&ctx_inner.state)[47] = ((uint8_t*)&ctx_inner_init.state)[47];
    ((uint8_t*)&ctx_inner.state)[48] = ((uint8_t*)&ctx_inner_init.state)[48];
    ((uint8_t*)&ctx_inner.state)[49] = ((uint8_t*)&ctx_inner_init.state)[49];
    ((uint8_t*)&ctx_inner.state)[50] = ((uint8_t*)&ctx_inner_init.state)[50];
    ((uint8_t*)&ctx_inner.state)[51] = ((uint8_t*)&ctx_inner_init.state)[51];
    ((uint8_t*)&ctx_inner.state)[52] = ((uint8_t*)&ctx_inner_init.state)[52];
    ((uint8_t*)&ctx_inner.state)[53] = ((uint8_t*)&ctx_inner_init.state)[53];
    ((uint8_t*)&ctx_inner.state)[54] = ((uint8_t*)&ctx_inner_init.state)[54];
    ((uint8_t*)&ctx_inner.state)[55] = ((uint8_t*)&ctx_inner_init.state)[55];
    ((uint8_t*)&ctx_inner.state)[56] = ((uint8_t*)&ctx_inner_init.state)[56];
    ((uint8_t*)&ctx_inner.state)[57] = ((uint8_t*)&ctx_inner_init.state)[57];
    ((uint8_t*)&ctx_inner.state)[58] = ((uint8_t*)&ctx_inner_init.state)[58];
    ((uint8_t*)&ctx_inner.state)[59] = ((uint8_t*)&ctx_inner_init.state)[59];
    ((uint8_t*)&ctx_inner.state)[60] = ((uint8_t*)&ctx_inner_init.state)[60];
    ((uint8_t*)&ctx_inner.state)[61] = ((uint8_t*)&ctx_inner_init.state)[61];
    ((uint8_t*)&ctx_inner.state)[62] = ((uint8_t*)&ctx_inner_init.state)[62];
    ((uint8_t*)&ctx_inner.state)[63] = ((uint8_t*)&ctx_inner_init.state)[63];
    ctx_inner.count[0] = 1024;
    ctx_inner.count[1] = 0;
    memset(ctx_inner.buffer, 0, 128);
    sha512_update(&ctx_inner, u, SHA512_HASH_LENGTH);
    sha512_final(&ctx_inner, u);
        
        // Outer hash - manual ctx copy (64 bytes state)
        ((uint8_t*)&ctx_outer.state)[0] = ((uint8_t*)&ctx_outer_init.state)[0];
        ((uint8_t*)&ctx_outer.state)[1] = ((uint8_t*)&ctx_outer_init.state)[1];
        ((uint8_t*)&ctx_outer.state)[2] = ((uint8_t*)&ctx_outer_init.state)[2];
        ((uint8_t*)&ctx_outer.state)[3] = ((uint8_t*)&ctx_outer_init.state)[3];
        ((uint8_t*)&ctx_outer.state)[4] = ((uint8_t*)&ctx_outer_init.state)[4];
        ((uint8_t*)&ctx_outer.state)[5] = ((uint8_t*)&ctx_outer_init.state)[5];
        ((uint8_t*)&ctx_outer.state)[6] = ((uint8_t*)&ctx_outer_init.state)[6];
        ((uint8_t*)&ctx_outer.state)[7] = ((uint8_t*)&ctx_outer_init.state)[7];
        ((uint8_t*)&ctx_outer.state)[8] = ((uint8_t*)&ctx_outer_init.state)[8];
        ((uint8_t*)&ctx_outer.state)[9] = ((uint8_t*)&ctx_outer_init.state)[9];
        ((uint8_t*)&ctx_outer.state)[10] = ((uint8_t*)&ctx_outer_init.state)[10];
        ((uint8_t*)&ctx_outer.state)[11] = ((uint8_t*)&ctx_outer_init.state)[11];
        ((uint8_t*)&ctx_outer.state)[12] = ((uint8_t*)&ctx_outer_init.state)[12];
        ((uint8_t*)&ctx_outer.state)[13] = ((uint8_t*)&ctx_outer_init.state)[13];
        ((uint8_t*)&ctx_outer.state)[14] = ((uint8_t*)&ctx_outer_init.state)[14];
        ((uint8_t*)&ctx_outer.state)[15] = ((uint8_t*)&ctx_outer_init.state)[15];
        ((uint8_t*)&ctx_outer.state)[16] = ((uint8_t*)&ctx_outer_init.state)[16];
        ((uint8_t*)&ctx_outer.state)[17] = ((uint8_t*)&ctx_outer_init.state)[17];
        ((uint8_t*)&ctx_outer.state)[18] = ((uint8_t*)&ctx_outer_init.state)[18];
        ((uint8_t*)&ctx_outer.state)[19] = ((uint8_t*)&ctx_outer_init.state)[19];
        ((uint8_t*)&ctx_outer.state)[20] = ((uint8_t*)&ctx_outer_init.state)[20];
        ((uint8_t*)&ctx_outer.state)[21] = ((uint8_t*)&ctx_outer_init.state)[21];
        ((uint8_t*)&ctx_outer.state)[22] = ((uint8_t*)&ctx_outer_init.state)[22];
        ((uint8_t*)&ctx_outer.state)[23] = ((uint8_t*)&ctx_outer_init.state)[23];
        ((uint8_t*)&ctx_outer.state)[24] = ((uint8_t*)&ctx_outer_init.state)[24];
        ((uint8_t*)&ctx_outer.state)[25] = ((uint8_t*)&ctx_outer_init.state)[25];
        ((uint8_t*)&ctx_outer.state)[26] = ((uint8_t*)&ctx_outer_init.state)[26];
        ((uint8_t*)&ctx_outer.state)[27] = ((uint8_t*)&ctx_outer_init.state)[27];
        ((uint8_t*)&ctx_outer.state)[28] = ((uint8_t*)&ctx_outer_init.state)[28];
        ((uint8_t*)&ctx_outer.state)[29] = ((uint8_t*)&ctx_outer_init.state)[29];
        ((uint8_t*)&ctx_outer.state)[30] = ((uint8_t*)&ctx_outer_init.state)[30];
        ((uint8_t*)&ctx_outer.state)[31] = ((uint8_t*)&ctx_outer_init.state)[31];
        ((uint8_t*)&ctx_outer.state)[32] = ((uint8_t*)&ctx_outer_init.state)[32];
        ((uint8_t*)&ctx_outer.state)[33] = ((uint8_t*)&ctx_outer_init.state)[33];
        ((uint8_t*)&ctx_outer.state)[34] = ((uint8_t*)&ctx_outer_init.state)[34];
        ((uint8_t*)&ctx_outer.state)[35] = ((uint8_t*)&ctx_outer_init.state)[35];
        ((uint8_t*)&ctx_outer.state)[36] = ((uint8_t*)&ctx_outer_init.state)[36];
        ((uint8_t*)&ctx_outer.state)[37] = ((uint8_t*)&ctx_outer_init.state)[37];
        ((uint8_t*)&ctx_outer.state)[38] = ((uint8_t*)&ctx_outer_init.state)[38];
        ((uint8_t*)&ctx_outer.state)[39] = ((uint8_t*)&ctx_outer_init.state)[39];
        ((uint8_t*)&ctx_outer.state)[40] = ((uint8_t*)&ctx_outer_init.state)[40];
        ((uint8_t*)&ctx_outer.state)[41] = ((uint8_t*)&ctx_outer_init.state)[41];
        ((uint8_t*)&ctx_outer.state)[42] = ((uint8_t*)&ctx_outer_init.state)[42];
        ((uint8_t*)&ctx_outer.state)[43] = ((uint8_t*)&ctx_outer_init.state)[43];
        ((uint8_t*)&ctx_outer.state)[44] = ((uint8_t*)&ctx_outer_init.state)[44];
        ((uint8_t*)&ctx_outer.state)[45] = ((uint8_t*)&ctx_outer_init.state)[45];
        ((uint8_t*)&ctx_outer.state)[46] = ((uint8_t*)&ctx_outer_init.state)[46];
        ((uint8_t*)&ctx_outer.state)[47] = ((uint8_t*)&ctx_outer_init.state)[47];
        ((uint8_t*)&ctx_outer.state)[48] = ((uint8_t*)&ctx_outer_init.state)[48];
        ((uint8_t*)&ctx_outer.state)[49] = ((uint8_t*)&ctx_outer_init.state)[49];
        ((uint8_t*)&ctx_outer.state)[50] = ((uint8_t*)&ctx_outer_init.state)[50];
        ((uint8_t*)&ctx_outer.state)[51] = ((uint8_t*)&ctx_outer_init.state)[51];
        ((uint8_t*)&ctx_outer.state)[52] = ((uint8_t*)&ctx_outer_init.state)[52];
        ((uint8_t*)&ctx_outer.state)[53] = ((uint8_t*)&ctx_outer_init.state)[53];
        ((uint8_t*)&ctx_outer.state)[54] = ((uint8_t*)&ctx_outer_init.state)[54];
        ((uint8_t*)&ctx_outer.state)[55] = ((uint8_t*)&ctx_outer_init.state)[55];
        ((uint8_t*)&ctx_outer.state)[56] = ((uint8_t*)&ctx_outer_init.state)[56];
        ((uint8_t*)&ctx_outer.state)[57] = ((uint8_t*)&ctx_outer_init.state)[57];
        ((uint8_t*)&ctx_outer.state)[58] = ((uint8_t*)&ctx_outer_init.state)[58];
        ((uint8_t*)&ctx_outer.state)[59] = ((uint8_t*)&ctx_outer_init.state)[59];
        ((uint8_t*)&ctx_outer.state)[60] = ((uint8_t*)&ctx_outer_init.state)[60];
        ((uint8_t*)&ctx_outer.state)[61] = ((uint8_t*)&ctx_outer_init.state)[61];
        ((uint8_t*)&ctx_outer.state)[62] = ((uint8_t*)&ctx_outer_init.state)[62];
        ((uint8_t*)&ctx_outer.state)[63] = ((uint8_t*)&ctx_outer_init.state)[63];
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
