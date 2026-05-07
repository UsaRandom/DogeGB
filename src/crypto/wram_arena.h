#ifndef WRAM_ARENA_H
#define WRAM_ARENA_H

#include <stdint.h>
#include "sha512.h"
#include "sha256.h"

/*
 * WRAM arena — wallet-generation and TX-signing are mutually exclusive at
 * runtime, so their large scratch buffers share the same WRAM via a union.
 * This saves ~1,478 bytes compared to allocating both sides separately.
 *
 * Wallet-gen side  ≈ 2,228 bytes  (active during mnemonic_to_seed / hd_wallet)
 * Signing side     ≈ 1,480 bytes  (active during sign_tx / ecdsa_sign)
 * Union size       = 2,228 bytes  (max of the two)
 */

typedef struct {
    /* sha512_transform.c — W[80] schedule buffer (640 bytes).
       Working vars a-h, T1, T2 are kept on the stack (80 bytes, safe). */
    uint64_t  sha512_W[80];

    /* pbkdf2.c */
    uint8_t   pbkdf2_u[64];
    uint8_t   pbkdf2_k_ipad[128];
    uint8_t   pbkdf2_k_opad[128];
    uint8_t   pbkdf2_tk[64];
    uint8_t   pbkdf2_inner_state[64];
    uint8_t   pbkdf2_outer_state[64];
    SHA512_CTX pbkdf2_ctx_inner;    /* 208 bytes */
    SHA512_CTX pbkdf2_ctx_outer;    /* 208 bytes */
    uint8_t   pbkdf2_salt_block[132];

    /* hmac.c */
    SHA512_CTX hmac_ctx;            /* 208 bytes */
    uint8_t   hmac_k_pad[128];
    uint8_t   hmac_tk[64];

    /* mnemonic.c */
    uint8_t   mnemonic_salt[128];
} WalletGenScratch;                 /* total: 2,228 bytes */

typedef struct {
    /* tx_signer.c */
    uint8_t    ts_sighash[432];     /* preimage buf: MAX_TX_LEN(400) + 32 */
    uint8_t    ts_sigs[4][73];      /* per-input DER sigs + SIGHASH byte  */
    uint8_t    ts_sig_lens[4];

    /* ecdsa.c — hmac_sha256_local */
    SHA256_CTX ec_hmac_ctx;         /* 108 bytes */
    uint8_t    ec_hmac_k_pad[64];
    uint8_t    ec_hmac_inner[32];

    /* ecdsa.c — n_reduce512 */
    uint8_t    ec_reduce_result[32];

    /* ecdsa.c — n_mulmod */
    uint8_t    ec_mul_prod[64];

    /* ecdsa.c — n_invmod */
    uint8_t    ec_inv_u[32];
    uint8_t    ec_inv_v[32];
    uint8_t    ec_inv_A[32];
    uint8_t    ec_inv_C[32];

    /* ecdsa.c — rfc6979_k */
    uint8_t    ec_rfc_V[32];
    uint8_t    ec_rfc_K[32];
    uint8_t    ec_rfc_msg[97];

    /* ecdsa.c — ecdsa_sign */
    uint8_t    ec_k[32];
    uint8_t    ec_r[32];
    uint8_t    ec_s[32];
    uint8_t    ec_pubkey[33];
    uint8_t    ec_k_inv[32];
} SigningScratch;                   /* total: 1,480 bytes */

typedef union {
    WalletGenScratch w;
    SigningScratch   s;
} CryptoArena;                     /* sizeof = 2,228 bytes */

extern CryptoArena g_arena;

#endif
