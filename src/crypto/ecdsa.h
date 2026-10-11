#ifndef ECDSA_H
#define ECDSA_H

#include <stdint.h>
#include <gb/gb.h>
#include "sign_mem.h"

/* Returns 0 when r or s is zero or no k in range turns up.
   The signature is DER plus the SIGHASH_ALL byte, at most 73 bytes. */
uint8_t ecdsa_sign_mem(SignMem *m) BANKED;

/* Double SHA-256 of the SIGHASH_ALL preimage into m->hash. */
uint8_t sighash_one(SignMem *m, const SighashIn *in) BANKED;

/* Harness. priv = 1, fixed message bytes. Not used by the wallet. */
uint8_t ecdsa_bench_sizes(uint8_t out[4]) BANKED;
uint8_t ecdsa_bench_rfc(uint8_t out[32]) BANKED;
uint8_t ecdsa_bench_inv(uint8_t out[32]) BANKED;
uint8_t ecdsa_bench_mul(uint8_t out[32]) BANKED;
uint8_t ecdsa_bench_hash(uint8_t out[32]) BANKED;
uint8_t ecdsa_bench_sign(uint8_t out[106]) BANKED;

#endif
