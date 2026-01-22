#ifndef HMAC_H
#define HMAC_H

#include <stdint.h>
#include <gb/gb.h>

void hmac_sha512(uint8_t *out, const uint8_t *key, uint32_t key_len, const uint8_t *data, uint32_t data_len) BANKED;

#endif
