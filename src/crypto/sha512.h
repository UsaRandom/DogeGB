#ifndef SHA512_H
#define SHA512_H

#include <stdint.h>
#include <gb/gb.h>

#define SHA512_HASH_LENGTH 64

typedef struct {
    uint64_t state[8];
    uint64_t count[2];
    uint8_t buffer[128];
} SHA512_CTX;

void sha512_init(SHA512_CTX *ctx) BANKED;
void sha512_update(SHA512_CTX *ctx, const uint8_t *data, uint32_t len) BANKED;
void sha512_final(SHA512_CTX *ctx, uint8_t *digest) BANKED;

#endif
