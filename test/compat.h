// Game Boy compatibility layer for macOS testing
#ifndef COMPAT_H
#define COMPAT_H

// Include standard headers first
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Stub out Game Boy banking macros
#define BANKREF(bank)
#define BANKREF_EXTERN(bank)
#define BANK(bank) 0
#define SWITCH_ROM(bank)

// Stub out CPU functions
#define cpu_fast()
#define _cpu 0

// Stub out interrupt control
#define __asm__(x)

// Stub out GBDK includes
#define __GBDK_VERSION__ 0

// Function stubs for Game Boy functions
void wait_vbl_done(void);
uint8_t joypad(void);

// SHA256 signature fix - declare with size_t to match implementation
struct SHA256_CTX;
void sha256_update(struct SHA256_CTX *ctx, const uint8_t data[], size_t len);

#endif
