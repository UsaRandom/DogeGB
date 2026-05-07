#pragma bank 5

#include "ecdsa.h"
#include "sha256.h"
#include "secp256k1.h"
#include "wram_arena.h"

#include <string.h>
#include <stdint.h>

static const uint8_t SECP256K1_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
    0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41
};

static const uint8_t SECP256K1_N_HALF[32] = {
    0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x5D,0x57,0x6E,0x73,0x57,0xA4,0x50,0x1D,
    0xDF,0xE9,0x2F,0x46,0x68,0x1B,0x20,0xA0
};

/* Arena aliases — signing side */
#define ec_reduce_result  g_arena.s.ec_reduce_result
#define ec_mul_prod       g_arena.s.ec_mul_prod
#define ec_inv_u          g_arena.s.ec_inv_u
#define ec_inv_v          g_arena.s.ec_inv_v
#define ec_inv_A          g_arena.s.ec_inv_A
#define ec_inv_C          g_arena.s.ec_inv_C
#define ec_hmac_ctx       g_arena.s.ec_hmac_ctx
#define ec_hmac_k_pad     g_arena.s.ec_hmac_k_pad
#define ec_hmac_inner     g_arena.s.ec_hmac_inner
#define ec_rfc_V          g_arena.s.ec_rfc_V
#define ec_rfc_K          g_arena.s.ec_rfc_K
#define ec_rfc_msg        g_arena.s.ec_rfc_msg
#define ec_k              g_arena.s.ec_k
#define ec_r              g_arena.s.ec_r
#define ec_s              g_arena.s.ec_s
#define ec_pubkey         g_arena.s.ec_pubkey
#define ec_k_inv          g_arena.s.ec_k_inv

/* ---- mod-n 256-bit arithmetic ---- */

static int n_cmp(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 32; i++) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

static int n_is_zero(const uint8_t *a) {
    for (int i = 0; i < 32; i++) if (a[i]) return 0;
    return 1;
}

static void n_copy(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < 32; i++) dst[i] = src[i];
}

static void n_shr1(uint8_t *a) {
    uint8_t carry = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t next = a[i] & 1;
        a[i] = (a[i] >> 1) | (carry << 7);
        carry = next;
    }
}

static uint8_t n_add(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint16_t carry = 0;
    for (int i = 31; i >= 0; i--) {
        carry += (uint16_t)a[i] + b[i];
        out[i] = carry & 0xFF;
        carry >>= 8;
    }
    return (uint8_t)carry;
}

static void n_sub(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint16_t borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int16_t d = (int16_t)a[i] - b[i] - borrow;
        if (d < 0) { d += 256; borrow = 1; } else borrow = 0;
        out[i] = (uint8_t)d;
    }
}

static void n_addmod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint8_t carry = n_add(out, a, b);
    if (carry || n_cmp(out, SECP256K1_N) >= 0)
        n_sub(out, out, SECP256K1_N);
}

static void n_submod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    if (n_cmp(a, b) >= 0) {
        n_sub(out, a, b);
    } else {
        uint8_t tmp[32];
        n_sub(tmp, b, a);
        n_sub(out, SECP256K1_N, tmp);
    }
}

static void n_reduce512(uint8_t *out, const uint8_t *prod) {
    memset(ec_reduce_result, 0, 32);
    for (int i = 0; i < 512; i++) {
        int byte_i = i / 8;
        int bit_i  = 7 - (i % 8);
        uint8_t bit = (prod[byte_i] >> bit_i) & 1;
        uint8_t carry = n_add(ec_reduce_result, ec_reduce_result, ec_reduce_result);
        if (carry || n_cmp(ec_reduce_result, SECP256K1_N) >= 0)
            n_sub(ec_reduce_result, ec_reduce_result, SECP256K1_N);
        if (bit) {
            uint8_t one[32]; memset(one, 0, 32); one[31] = 1;
            n_addmod(ec_reduce_result, ec_reduce_result, one);
        }
    }
    n_copy(out, ec_reduce_result);
}

static void n_mulmod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    memset(ec_mul_prod, 0, 64);
    for (int i = 31; i >= 0; i--) {
        uint32_t carry = 0;
        for (int j = 31; j >= 0; j--) {
            uint32_t s = ec_mul_prod[i+j+1] + (uint32_t)a[i]*b[j] + carry;
            ec_mul_prod[i+j+1] = s & 0xFF;
            carry = s >> 8;
        }
        ec_mul_prod[i] += (uint8_t)carry;
    }
    n_reduce512(out, ec_mul_prod);
}

static void n_half(uint8_t *x) {
    uint8_t carry = 0;
    if (x[31] & 1) {
        carry = n_add(x, x, SECP256K1_N);
    }
    n_shr1(x);
    if (carry) x[0] |= 0x80;
    if (n_cmp(x, SECP256K1_N) >= 0) n_sub(x, x, SECP256K1_N);
}

static void n_invmod(uint8_t *out, const uint8_t *a) {
    n_copy(ec_inv_u, a);
    n_copy(ec_inv_v, SECP256K1_N);
    memset(ec_inv_A, 0, 32); ec_inv_A[31] = 1;
    memset(ec_inv_C, 0, 32);

    while (!n_is_zero(ec_inv_u)) {
        while (!(ec_inv_u[31] & 1)) {
            n_shr1(ec_inv_u);
            n_half(ec_inv_A);
        }
        while (!(ec_inv_v[31] & 1)) {
            n_shr1(ec_inv_v);
            n_half(ec_inv_C);
        }
        if (n_cmp(ec_inv_u, ec_inv_v) >= 0) {
            n_sub(ec_inv_u, ec_inv_u, ec_inv_v);
            n_submod(ec_inv_A, ec_inv_A, ec_inv_C);
        } else {
            n_sub(ec_inv_v, ec_inv_v, ec_inv_u);
            n_submod(ec_inv_C, ec_inv_C, ec_inv_A);
        }
    }
    n_copy(out, ec_inv_C);
}

/* ---- HMAC-SHA256 (local, signing side only) ---- */

static void hmac_sha256_local(uint8_t out[32],
                               const uint8_t *key, uint8_t key_len,
                               const uint8_t *data, uint16_t data_len) {
    memset(ec_hmac_k_pad, 0, 64);
    memcpy(ec_hmac_k_pad, key, key_len);

    for (int i = 0; i < 64; i++) ec_hmac_k_pad[i] ^= 0x36;
    sha256_init(&ec_hmac_ctx);
    sha256_update(&ec_hmac_ctx, ec_hmac_k_pad, 64);
    sha256_update(&ec_hmac_ctx, data, data_len);
    sha256_final(&ec_hmac_ctx, ec_hmac_inner);

    for (int i = 0; i < 64; i++) ec_hmac_k_pad[i] ^= (0x36 ^ 0x5c);
    sha256_init(&ec_hmac_ctx);
    sha256_update(&ec_hmac_ctx, ec_hmac_k_pad, 64);
    sha256_update(&ec_hmac_ctx, ec_hmac_inner, 32);
    sha256_final(&ec_hmac_ctx, out);
}

/* ---- RFC 6979 deterministic k ---- */

static void rfc6979_k(const uint8_t privkey[32], const uint8_t hash[32],
                      uint8_t k_out[32]) {
    memset(ec_rfc_V, 0x01, 32);
    memset(ec_rfc_K, 0x00, 32);

    memcpy(ec_rfc_msg,      ec_rfc_V,   32);
    ec_rfc_msg[32] = 0x00;
    memcpy(ec_rfc_msg + 33, privkey,    32);
    memcpy(ec_rfc_msg + 65, hash,       32);
    hmac_sha256_local(ec_rfc_K, ec_rfc_K, 32, ec_rfc_msg, 97);

    hmac_sha256_local(ec_rfc_V, ec_rfc_K, 32, ec_rfc_V, 32);

    memcpy(ec_rfc_msg,      ec_rfc_V,   32);
    ec_rfc_msg[32] = 0x01;
    memcpy(ec_rfc_msg + 33, privkey,    32);
    memcpy(ec_rfc_msg + 65, hash,       32);
    hmac_sha256_local(ec_rfc_K, ec_rfc_K, 32, ec_rfc_msg, 97);

    hmac_sha256_local(ec_rfc_V, ec_rfc_K, 32, ec_rfc_V, 32);

    for (;;) {
        hmac_sha256_local(ec_rfc_V, ec_rfc_K, 32, ec_rfc_V, 32);
        if (!n_is_zero(ec_rfc_V) && n_cmp(ec_rfc_V, SECP256K1_N) < 0) {
            n_copy(k_out, ec_rfc_V);
            return;
        }
        memcpy(ec_rfc_msg, ec_rfc_V, 32);
        ec_rfc_msg[32] = 0x00;
        hmac_sha256_local(ec_rfc_K, ec_rfc_K, 32, ec_rfc_msg, 33);
        hmac_sha256_local(ec_rfc_V, ec_rfc_K, 32, ec_rfc_V, 32);
    }
}

/* ---- DER encoding ---- */

static uint8_t der_encode(const uint8_t r[32], const uint8_t s[32],
                           uint8_t *out) {
    uint8_t rbuf[33], sbuf[33];
    uint8_t rlen, slen;
    int ri = 0, si = 0;

    while (ri < 31 && r[ri] == 0) ri++;
    while (si < 31 && s[si] == 0) si++;

    if (r[ri] & 0x80) {
        rbuf[0] = 0x00;
        memcpy(rbuf + 1, r + ri, 32 - ri);
        rlen = 33 - ri;
    } else {
        memcpy(rbuf, r + ri, 32 - ri);
        rlen = 32 - ri;
    }

    if (s[si] & 0x80) {
        sbuf[0] = 0x00;
        memcpy(sbuf + 1, s + si, 32 - si);
        slen = 33 - si;
    } else {
        memcpy(sbuf, s + si, 32 - si);
        slen = 32 - si;
    }

    uint8_t total = 4 + rlen + slen;
    uint8_t *p = out;
    *p++ = 0x30;
    *p++ = total;
    *p++ = 0x02;
    *p++ = rlen;
    memcpy(p, rbuf, rlen); p += rlen;
    *p++ = 0x02;
    *p++ = slen;
    memcpy(p, sbuf, slen); p += slen;
    *p++ = 0x01; /* SIGHASH_ALL */
    return (uint8_t)(p - out);
}

/* ---- Public API ---- */

uint8_t ecdsa_sign(const uint8_t privkey[32], const uint8_t hash[32],
                   uint8_t sig_out[73]) BANKED {
    rfc6979_k(privkey, hash, ec_k);

    secp256k1_pubkey(ec_k, ec_pubkey);
    memcpy(ec_r, ec_pubkey + 1, 32);
    if (n_cmp(ec_r, SECP256K1_N) >= 0) n_sub(ec_r, ec_r, SECP256K1_N);

    n_invmod(ec_k_inv, ec_k);
    n_mulmod(ec_s, ec_r, privkey);
    n_addmod(ec_s, ec_s, hash);
    n_mulmod(ec_s, ec_k_inv, ec_s);

    if (n_cmp(ec_s, SECP256K1_N_HALF) > 0)
        n_sub(ec_s, SECP256K1_N, ec_s);

    return der_encode(ec_r, ec_s, sig_out);
}

#undef ec_reduce_result
#undef ec_mul_prod
#undef ec_inv_u
#undef ec_inv_v
#undef ec_inv_A
#undef ec_inv_C
#undef ec_hmac_ctx
#undef ec_hmac_k_pad
#undef ec_hmac_inner
#undef ec_rfc_V
#undef ec_rfc_K
#undef ec_rfc_msg
#undef ec_k
#undef ec_r
#undef ec_s
#undef ec_pubkey
#undef ec_k_inv
