#pragma bank 5
#include <stdio.h>
#include <string.h>

#include "pbkdf2.h"
#include "src/crypto/bip39_wordlist.h"
#include "src/crypto/sha256.h"

#include "mnemonic.h"
#include "entropy_data.h"

// Extern declaration for entropy pool
extern uint8_t entropy_pool[ENTROPY_POOL_SIZE];
extern uint8_t build_salt[BUILD_SALT_SIZE];


void my_memcpy(uint8_t *dest, const uint8_t *src, int len) {
    for (int i = 0; i < len; i++) {
        dest[i] = src[i];
    }
}
void my_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0'; 
}

int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

uint8_t simple_rand(void) {
    static uint16_t seed = 12345;
    seed = seed * 1103515245 + 12345;
    return ((*((volatile uint8_t*)0xFF04) ^ seed) & 0xFF);
}

void do_sha256(const uint8_t *data, uint16_t len, uint8_t *hash) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
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

    
    uint8_t hash[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, entropy_pool, ENTROPY_POOL_SIZE);
    sha256_final(&ctx, hash);

    for (uint16_t i = 0; i < ENTROPY_POOL_SIZE; i++) {
        entropy_pool[i] ^= hash[i & 31];
        entropy_pool[i] += hash[(i + 7) & 31];
    }

    uint16_t bit_pos = 0;
    for (uint8_t w = 0; w < 12; w++) {
        uint16_t word_idx = 0;

        for (uint8_t b = 0; b < 11; b++) {
            uint8_t byte_idx = bit_pos >> 3;
            uint8_t shift = 7 - (bit_pos & 7);

            if (hash[byte_idx] & (1u << shift)) {
                word_idx |= (1u << (10 - b));
            }

            bit_pos++;
        }

        get_bip39_word(word_idx, words[w]);
    }
}

void mnemonic_to_seed(const char *mnemonic, uint8_t *seed) BANKED {
    const char *salt_prefix = "mnemonic";
    static uint8_t salt[128]; 

    uint8_t salt_len = my_strlen(salt_prefix);
    my_memcpy(salt, (const uint8_t*)salt_prefix, salt_len);
    salt[salt_len] = '\0';


    // Disable interrupts during heavy crypto to avoid stack corruption
    #ifndef __APPLE__
    __asm__("di");
    #endif
    pbkdf2_hmac_sha512(seed, (const uint8_t*)mnemonic, my_strlen(mnemonic),
                       salt, salt_len, 2048);
    #ifndef __APPLE__
    __asm__("ei");
    #endif
}
