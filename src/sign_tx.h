#ifndef SIGN_TX_H
#define SIGN_TX_H

#include <stdint.h>
#include <gb/gb.h>

/* Decrypt the slot key, sighash each input, and sign it.
   Returns 0 if the slot or the signature is unusable.
   The key is wiped before return. On success the scratch just past
   SignMem holds the export: 33-byte compressed pubkey, then each
   signature in input order. A signature is DER and already ends in
   the SIGHASH_ALL byte. *blob_len is that many bytes. The caller
   wipes the scratch when it is done showing them. */
uint8_t sign_payment(uint8_t slot, uint8_t n_in, const uint8_t *txid,
                     const uint16_t *vout, uint8_t has_change, uint8_t dest_type,
                     const uint8_t pay[8], const uint8_t change[8],
                     const uint8_t dest[20], uint16_t *blob_len) BANKED;

#endif
