#ifndef SECP256K1_H
#define SECP256K1_H

#include <stdint.h>
#include <gb/gb.h>

// secp256k1 curve parameters (256-bit)
// p = FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFE FFFFFC2F
// G = (79BE667E F9DCBBAC 55A06295 CE870B07 029BFCDB 2DCE28D9 59F2815B 16F81798,
//      483ADA77 26A3C465 5DA4FBFC 0E1108A8 FD17B448 A6855419 9C47D08F FB10D4B8)

// Generate compressed public key from private key
// privkey: 32 bytes input
// pubkey: 33 bytes output (02/03 prefix + 32 bytes X)
void secp256k1_pubkey(const uint8_t *privkey, uint8_t *pubkey) BANKED;

#endif
