#ifndef ECDSA_H
#define ECDSA_H

#include <stdint.h>
#include <gb/gb.h>

// Sign a 32-byte hash with a 32-byte secp256k1 private key.
// sig_out: DER-encoded signature + 0x01 SIGHASH_ALL byte
// Returns: length of sig_out (71 or 72 bytes typically)
uint8_t ecdsa_sign(const uint8_t privkey[32], const uint8_t hash[32],
                   uint8_t sig_out[73]) BANKED;

#endif
