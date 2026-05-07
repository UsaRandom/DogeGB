#pragma bank 4
#include <stdio.h>
#include <string.h>

#include "pbkdf2.h"
#include "src/crypto/bip39_wordlist.h"
#include "src/crypto/sha256.h"

#include "mnemonic.h"
#include "entropy_data.h"
#include "hmac.h"
#include "wram_arena.h"

// Extern declaration for entropy pool
extern uint8_t entropy_pool[ENTROPY_POOL_SIZE];
extern uint8_t build_salt[BUILD_SALT_SIZE];


void my_memcpy(uint8_t *dest, const uint8_t *src, int len) {
    for (int i = 0; i < len; i++) {
        dest[i] = src[i];
    }
}

int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}


void generate_mnemonic(char words[12][9]) BANKED {
    
    for (uint16_t i = 0; i < ENTROPY_POOL_SIZE; i++) {
        entropy_pool[i] ^= entropy_pool[(i + 37) & (ENTROPY_POOL_SIZE - 1)];
        entropy_pool[i] += entropy_pool[(i + 113) & (ENTROPY_POOL_SIZE - 1)];
    }
    for (uint16_t i = 0; i < ENTROPY_POOL_SIZE; i++) {
        entropy_pool[i] ^= build_salt[i % BUILD_SALT_SIZE];
        entropy_pool[i] += build_salt[(i + 13) % BUILD_SALT_SIZE];
    }

    uint8_t hash[64];
    hmac_sha512(hash, build_salt, BUILD_SALT_SIZE, entropy_pool, ENTROPY_POOL_SIZE);

    uint8_t entropy[16];
    memcpy(entropy, hash, 16); 

    uint8_t cs_hash[32];
    SHA256_CTX cs_ctx;
    sha256_init(&cs_ctx);
    sha256_update(&cs_ctx, entropy, 16);
    sha256_final(&cs_ctx, cs_hash);
    uint8_t checksum = cs_hash[0] >> 4;

    for (uint16_t i = 0; i < ENTROPY_POOL_SIZE; i++) {
        entropy_pool[i] ^= hash[i & 63];      // updated to 64-byte hash
        entropy_pool[i] += hash[(i + 7) & 63];
    }

    uint16_t bit_pos = 0;
    for (uint8_t w = 0; w < 12; w++) {
        uint16_t word_idx = 0;
        for (uint8_t b = 0; b < 11; b++) {
            uint8_t bit;
            if (bit_pos < 128) {
                uint8_t byte_idx = bit_pos >> 3;
                uint8_t shift = 7 - (bit_pos & 7);
                bit = (entropy[byte_idx] & (1u << shift)) ? 1 : 0;
            } else {
                uint8_t cs_bit_pos = bit_pos - 128;
                bit = (checksum & (1u << (3 - cs_bit_pos))) ? 1 : 0;
            }

            if (bit) {
                word_idx |= (1u << (10 - b));
            }
            bit_pos++;
        }
        get_bip39_word(word_idx, words[w]);
    }
}


void mnemonic_to_seed(const char *mnemonic, uint8_t *seed) BANKED {
    const char *salt_prefix = "mnemonic";
    uint8_t *salt = g_arena.w.mnemonic_salt;

    uint8_t salt_len = my_strlen(salt_prefix);
    my_memcpy(salt, (const uint8_t*)salt_prefix, salt_len);
    salt[salt_len] = '\0';


    // Disable interrupts during heavy crypto to avoid stack corruption
    #ifndef TEST_MODE
    __asm__("di");
    #endif
    pbkdf2_hmac_sha512(seed, (const uint8_t*)mnemonic, my_strlen(mnemonic),
                       salt, salt_len, 2048);
    #ifndef TEST_MODE
    __asm__("ei");
    #endif
}
