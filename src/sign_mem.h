#ifndef SIGN_MEM_H
#define SIGN_MEM_H

#include <stdint.h>
#include "sha256.h"

/* One signature at a time. These bytes are the PBKDF2 block in
   pbkdf2.c, borrowed for the duration of a sign and wiped after.
   A second static copy of this struct would push the menu tile
   offset into the stack again. priv, the sighash, k, r, s, and the
   SHA-256 context stay outside the work union so a phase can reuse
   the preimage bytes without erasing them. */
#define SIGN_PRE_MAX 288u

typedef struct {
    uint8_t n_in;
    uint8_t has_change;
    uint8_t dest_type;
    uint8_t which;
    const uint8_t *txid;
    const uint16_t *vout;
    const uint8_t *pay;
    const uint8_t *change;
    const uint8_t *dest;
} SighashIn;

typedef struct {
    uint8_t priv[32];
    uint8_t pub[33];
    uint8_t h160[20];
    uint8_t hash[32];
    uint8_t k[32];
    uint8_t r[32];
    uint8_t s[32];
    uint8_t k_inv[32];
    uint8_t V[32];
    uint8_t K[32];
    uint8_t sig[73];
    uint8_t sig_len;
    SHA256_CTX ctx;
    union {
        uint8_t pre[SIGN_PRE_MAX];
        struct {
            uint8_t msg[97];
            uint8_t k_pad[64];
            uint8_t inner[32];
        } mac;
        struct {
            uint8_t u[32];
            uint8_t v[32];
            uint8_t A[32];
            uint8_t C[32];
            uint8_t prod[64];
            uint8_t result[32];
            uint8_t tmp[32];
            uint8_t rbuf[33];
            uint8_t sbuf[33];
        } ec;
    } work;
} SignMem;

#endif
