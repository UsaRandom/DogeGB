#ifndef TX_SIGNER_H
#define TX_SIGNER_H

#include <stdint.h>
#include <gb/gb.h>

// Sign all P2PKH inputs of an unsigned Dogecoin/Pepecoin/Bellscoin transaction.
//
// unsigned_tx / tx_len : raw unsigned transaction bytes
// privkey[32]          : secp256k1 private key
// pubkey[33]           : compressed public key (must match privkey)
// out / out_max        : output buffer
// out_len              : bytes written on success
//
// Returns 1 on success, 0 on error (parse failure, buffer too small, etc.)
uint8_t sign_tx(const uint8_t *unsigned_tx, uint16_t tx_len,
                const uint8_t privkey[32], const uint8_t pubkey[33],
                uint8_t *out, uint16_t out_max, uint16_t *out_len) BANKED;

#endif
