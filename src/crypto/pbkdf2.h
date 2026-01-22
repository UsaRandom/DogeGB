#ifndef PBKDF2_H
#define PBKDF2_H

#include <stdint.h>
#include <gb/gb.h>

// PBKDF2-HMAC-SHA512 with 2048 iterations (BIP39 standard)
void pbkdf2_hmac_sha512(uint8_t *out, const uint8_t *password, uint32_t password_len, const uint8_t *salt, uint32_t salt_len, uint32_t iterations) BANKED;

#endif
