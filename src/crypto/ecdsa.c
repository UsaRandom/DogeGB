#pragma bank 15

#include "ecdsa.h"
#include "pbkdf2.h"
#include "secp256k1.h"
#include "progress.h"

#include <string.h>

/* secp256k1 order N, and N/2 for low-S. */
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

static int n_cmp(const uint8_t *a, const uint8_t *b) {
    uint8_t i;
    for (i = 0; i < 32u; i++) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

static uint8_t n_is_zero(const uint8_t *a) {
    uint8_t i;
    for (i = 0; i < 32u; i++) {
        if (a[i]) return 0;
    }
    return 1;
}

static void n_copy(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, 32);
}

static void n_shr1(uint8_t *a) {
    uint8_t carry = 0;
    uint8_t i;
    for (i = 0; i < 32u; i++) {
        uint8_t next = (uint8_t)(a[i] & 1u);
        a[i] = (uint8_t)((a[i] >> 1) | (uint8_t)(carry << 7));
        carry = next;
    }
}

static uint8_t n_add(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint8_t i = 32;
    uint16_t carry = 0;
    while (i--) {
        carry = (uint16_t)(carry + a[i] + b[i]);
        out[i] = (uint8_t)carry;
        carry >>= 8;
    }
    return (uint8_t)carry;
}

static void n_sub(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint8_t i = 32;
    uint8_t borrow = 0;
    while (i--) {
        uint16_t d = (uint16_t)((uint16_t)a[i] - b[i] - borrow);
        borrow = (uint8_t)((d >> 8) ? 1u : 0u);
        out[i] = (uint8_t)d;
    }
}

static void n_addmod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint8_t carry = n_add(out, a, b);
    if (carry || n_cmp(out, SECP256K1_N) >= 0)
        n_sub(out, out, SECP256K1_N);
}

/* Add 1 in place, then fold once if the sum reached N. */
static void n_add1(uint8_t *x) {
    uint8_t i = 32;
    uint16_t carry = 1;
    while (i--) {
        carry = (uint16_t)(carry + x[i]);
        x[i] = (uint8_t)carry;
        carry >>= 8;
        if (carry == 0) break;
    }
    if (carry || n_cmp(x, SECP256K1_N) >= 0)
        n_sub(x, x, SECP256K1_N);
}

static void n_submod(SignMem *m, uint8_t *out, const uint8_t *a, const uint8_t *b) {
    if (n_cmp(a, b) >= 0) {
        n_sub(out, a, b);
    } else {
        n_sub(m->work.ec.tmp, b, a);
        n_sub(out, SECP256K1_N, m->work.ec.tmp);
    }
}

/* 512 one-bit reductions. A tick every 16 bits, 32 ticks per multiply. */
static void n_reduce512(SignMem *m, uint8_t *out, const uint8_t *prod) {
    uint8_t *result = m->work.ec.result;
    uint16_t i;
    memset(result, 0, 32);
    for (i = 0; i < 512u; i++) {
        uint8_t byte_i = (uint8_t)(i >> 3);
        uint8_t bit_i = (uint8_t)(7u - (i & 7u));
        uint8_t bit = (uint8_t)((prod[byte_i] >> bit_i) & 1u);
        uint8_t carry = n_add(result, result, result);
        if (carry || n_cmp(result, SECP256K1_N) >= 0)
            n_sub(result, result, SECP256K1_N);
        if (bit) n_add1(result);
        if ((i & 15u) == 15u) add_progress(WEIGHT_SIGN_BIT);
    }
    n_copy(out, result);
}

static void n_mulmod(SignMem *m, uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint8_t *prod = m->work.ec.prod;
    uint8_t i = 32;
    memset(prod, 0, 64);
    while (i--) {
        uint8_t j = 32;
        uint32_t carry = 0;
        while (j--) {
            /* uint8 multiply promotes to a 16-bit int and overflows. */
            uint32_t s = (uint32_t)prod[i + j + 1u] + (uint32_t)a[i] * (uint32_t)b[j] + carry;
            prod[i + j + 1u] = (uint8_t)s;
            carry = s >> 8;
        }
        prod[i] = (uint8_t)(prod[i] + (uint8_t)carry);
    }
    n_reduce512(m, out, prod);
}

static void n_half(uint8_t *x) {
    uint8_t carry = 0;
    if (x[31] & 1u) carry = n_add(x, x, SECP256K1_N);
    n_shr1(x);
    if (carry) x[0] = (uint8_t)(x[0] | 0x80u);
    if (n_cmp(x, SECP256K1_N) >= 0) n_sub(x, x, SECP256K1_N);
}

static void n_invmod(SignMem *m, uint8_t *out, const uint8_t *a) {
    uint8_t *u = m->work.ec.u;
    uint8_t *v = m->work.ec.v;
    uint8_t *A = m->work.ec.A;
    uint8_t *C = m->work.ec.C;
    n_copy(u, a);
    n_copy(v, SECP256K1_N);
    memset(A, 0, 32);
    A[31] = 1;
    memset(C, 0, 32);
    while (!n_is_zero(u)) {
        while (!(u[31] & 1u)) {
            n_shr1(u);
            n_half(A);
        }
        while (!(v[31] & 1u)) {
            n_shr1(v);
            n_half(C);
        }
        if (n_cmp(u, v) >= 0) {
            n_sub(u, u, v);
            n_submod(m, A, A, C);
        } else {
            n_sub(v, v, u);
            n_submod(m, C, C, A);
        }
    }
    n_copy(out, C);
    add_progress(WEIGHT_SIGN_INV);
}

/* Key is at most 32 bytes. data must not alias k_pad or inner. */
static void hmac_sha256(SignMem *m, uint8_t *out,
                        const uint8_t *key, uint8_t key_len,
                        const uint8_t *data, uint8_t data_len) {
    uint8_t i;
    memset(m->work.mac.k_pad, 0, 64);
    memcpy(m->work.mac.k_pad, key, key_len);
    for (i = 0; i < 64u; i++)
        m->work.mac.k_pad[i] ^= 0x36u;
    sha256_init(&m->ctx);
    sha256_update(&m->ctx, m->work.mac.k_pad, 64);
    sha256_update(&m->ctx, data, data_len);
    sha256_final(&m->ctx, m->work.mac.inner);
    for (i = 0; i < 64u; i++)
        m->work.mac.k_pad[i] ^= (uint8_t)(0x36u ^ 0x5cu);
    sha256_init(&m->ctx);
    sha256_update(&m->ctx, m->work.mac.k_pad, 64);
    sha256_update(&m->ctx, m->work.mac.inner, 32);
    sha256_final(&m->ctx, out);
}

static void rfc_setup(SignMem *m) {
    memset(m->V, 0x01, 32);
    memset(m->K, 0x00, 32);

    memcpy(m->work.mac.msg, m->V, 32);
    m->work.mac.msg[32] = 0x00;
    memcpy(m->work.mac.msg + 33, m->priv, 32);
    memcpy(m->work.mac.msg + 65, m->hash, 32);
    hmac_sha256(m, m->K, m->K, 32, m->work.mac.msg, 97);
    hmac_sha256(m, m->V, m->K, 32, m->V, 32);

    memcpy(m->work.mac.msg, m->V, 32);
    m->work.mac.msg[32] = 0x01;
    memcpy(m->work.mac.msg + 33, m->priv, 32);
    memcpy(m->work.mac.msg + 65, m->hash, 32);
    hmac_sha256(m, m->K, m->K, 32, m->work.mac.msg, 97);
    hmac_sha256(m, m->V, m->K, 32, m->V, 32);
}

static uint8_t rfc_candidate(SignMem *m) {
    hmac_sha256(m, m->V, m->K, 32, m->V, 32);
    if (n_is_zero(m->V) || n_cmp(m->V, SECP256K1_N) >= 0) return 0;
    memcpy(m->k, m->V, 32);
    return 1;
}

static void rfc_bump(SignMem *m) {
    memcpy(m->work.mac.msg, m->V, 32);
    m->work.mac.msg[32] = 0x00;
    hmac_sha256(m, m->K, m->K, 32, m->work.mac.msg, 33);
    hmac_sha256(m, m->V, m->K, 32, m->V, 32);
}

static uint8_t der_encode(SignMem *m) {
    uint8_t *rbuf = m->work.ec.rbuf;
    uint8_t *sbuf = m->work.ec.sbuf;
    uint8_t ri = 0;
    uint8_t si = 0;
    uint8_t rlen;
    uint8_t slen;
    uint8_t total;
    uint8_t *p;

    while (ri < 31u && m->r[ri] == 0) ri++;
    while (si < 31u && m->s[si] == 0) si++;

    if (m->r[ri] & 0x80u) {
        rbuf[0] = 0x00;
        memcpy(rbuf + 1, m->r + ri, (uint8_t)(32u - ri));
        rlen = (uint8_t)(33u - ri);
    } else {
        memcpy(rbuf, m->r + ri, (uint8_t)(32u - ri));
        rlen = (uint8_t)(32u - ri);
    }
    if (m->s[si] & 0x80u) {
        sbuf[0] = 0x00;
        memcpy(sbuf + 1, m->s + si, (uint8_t)(32u - si));
        slen = (uint8_t)(33u - si);
    } else {
        memcpy(sbuf, m->s + si, (uint8_t)(32u - si));
        slen = (uint8_t)(32u - si);
    }

    total = (uint8_t)(4u + rlen + slen);
    p = m->sig;
    *p++ = 0x30;
    *p++ = total;
    *p++ = 0x02;
    *p++ = rlen;
    memcpy(p, rbuf, rlen);
    p += rlen;
    *p++ = 0x02;
    *p++ = slen;
    memcpy(p, sbuf, slen);
    p += slen;
    *p++ = 0x01;
    m->sig_len = (uint8_t)(p - m->sig);
    return m->sig_len;
}

uint8_t ecdsa_sign_mem(SignMem *m) BANKED {
    uint8_t attempt;

    rfc_setup(m);
    add_progress(WEIGHT_SIGN_RFC);
    if (n_cmp(m->hash, SECP256K1_N) >= 0)
        n_sub(m->hash, m->hash, SECP256K1_N);

    for (attempt = 0; attempt < 8u; attempt++) {
        if (!rfc_candidate(m)) {
            rfc_bump(m);
            continue;
        }
        secp256k1_pubkey(m->k, m->pub);
        memcpy(m->r, m->pub + 1, 32);
        if (n_cmp(m->r, SECP256K1_N) >= 0)
            n_sub(m->r, m->r, SECP256K1_N);
        if (n_is_zero(m->r)) {
            rfc_bump(m);
            continue;
        }
        n_invmod(m, m->k_inv, m->k);
        n_mulmod(m, m->s, m->r, m->priv);
        n_addmod(m->s, m->s, m->hash);
        n_mulmod(m, m->s, m->k_inv, m->s);
        if (n_cmp(m->s, SECP256K1_N_HALF) > 0)
            n_sub(m->s, SECP256K1_N, m->s);
        if (n_is_zero(m->s)) {
            rfc_bump(m);
            continue;
        }
        return der_encode(m) ? 1u : 0u;
    }
    return 0;
}

static uint8_t emit(SignMem *m, uint16_t *n, const uint8_t *src, uint8_t len) {
    if ((uint16_t)(*n + len) > SIGN_PRE_MAX) return 0;
    memcpy(m->work.pre + *n, src, len);
    *n = (uint16_t)(*n + len);
    return 1;
}

static uint8_t emit_byte(SignMem *m, uint16_t *n, uint8_t b) {
    if (*n >= SIGN_PRE_MAX) return 0;
    m->work.pre[(*n)++] = b;
    return 1;
}

static uint8_t emit_rev(SignMem *m, uint16_t *n, const uint8_t *src, uint8_t len) {
    uint8_t i;
    if ((uint16_t)(*n + len) > SIGN_PRE_MAX) return 0;
    for (i = 0; i < len; i++)
        m->work.pre[*n + i] = src[(uint8_t)(len - 1u - i)];
    *n = (uint16_t)(*n + len);
    return 1;
}

static uint8_t emit_script(SignMem *m, uint16_t *n, uint8_t kind, const uint8_t h160[20]) {
    if (kind == 0xFFu) return emit_byte(m, n, 0x00);
    if (kind == 1u) {
        if (!emit_byte(m, n, 0x17)) return 0;
        if (!emit_byte(m, n, 0xa9)) return 0;
        if (!emit_byte(m, n, 0x14)) return 0;
        if (!emit(m, n, h160, 20)) return 0;
        return emit_byte(m, n, 0x87);
    }
    if (!emit_byte(m, n, 0x19)) return 0;
    if (!emit_byte(m, n, 0x76)) return 0;
    if (!emit_byte(m, n, 0xa9)) return 0;
    if (!emit_byte(m, n, 0x14)) return 0;
    if (!emit(m, n, h160, 20)) return 0;
    if (!emit_byte(m, n, 0x88)) return 0;
    return emit_byte(m, n, 0xac);
}

static uint8_t emit_u32(SignMem *m, uint16_t *n, uint32_t v) {
    if (!emit_byte(m, n, (uint8_t)v)) return 0;
    if (!emit_byte(m, n, (uint8_t)(v >> 8))) return 0;
    if (!emit_byte(m, n, (uint8_t)(v >> 16))) return 0;
    return emit_byte(m, n, (uint8_t)(v >> 24));
}

uint8_t sighash_one(SignMem *m, const SighashIn *in) BANKED {
    uint16_t n = 0;
    uint8_t i;

    if (in->n_in < 1u || in->n_in > 4u || in->which >= in->n_in) return 0;
    if (in->dest_type > 1u) return 0;

    if (!emit_u32(m, &n, 1UL)) return 0;
    if (!emit_byte(m, &n, in->n_in)) return 0;
    for (i = 0; i < in->n_in; i++) {
        const uint8_t *txid = in->txid + (uint16_t)i * 32u;
        if (!emit_rev(m, &n, txid, 32)) return 0;
        if (!emit_u32(m, &n, in->vout[i])) return 0;
        if (i == in->which) {
            if (!emit_script(m, &n, 0u, m->h160)) return 0;
        } else if (!emit_script(m, &n, 0xFFu, 0)) {
            return 0;
        }
        if (!emit_u32(m, &n, 0xFFFFFFFFUL)) return 0;
    }
    if (!emit_byte(m, &n, in->has_change ? 2u : 1u)) return 0;
    if (!emit_rev(m, &n, in->pay, 8)) return 0;
    if (!emit_script(m, &n, in->dest_type, in->dest)) return 0;
    if (in->has_change) {
        if (!emit_rev(m, &n, in->change, 8)) return 0;
        if (!emit_script(m, &n, 0u, m->h160)) return 0;
    }
    if (!emit_u32(m, &n, 0UL)) return 0;
    if (!emit_u32(m, &n, 1UL)) return 0;

    sha256_init(&m->ctx);
    sha256_update(&m->ctx, m->work.pre, n);
    sha256_final(&m->ctx, m->hash);
    sha256_init(&m->ctx);
    sha256_update(&m->ctx, m->hash, 32);
    sha256_final(&m->ctx, m->hash);
    add_progress(WEIGHT_SIGN_HASH);
    return 1;
}

static SignMem *borrow(void) {
    uint16_t len = 0;
    uint8_t *base = kdf_scratch(&len);
    if (len < sizeof(SignMem)) return 0;
    return (SignMem *)base;
}

uint8_t ecdsa_bench_sizes(uint8_t out[4]) BANKED {
    uint16_t len = 0;
    uint16_t need;
    kdf_scratch(&len);
    need = (uint16_t)sizeof(SignMem);
    out[0] = (uint8_t)len;
    out[1] = (uint8_t)(len >> 8);
    out[2] = (uint8_t)need;
    out[3] = (uint8_t)(need >> 8);
    return len >= need;
}

uint8_t ecdsa_bench_rfc(uint8_t out[32]) BANKED {
    SignMem *m = borrow();
    uint8_t i;
    if (!m) return 0;
    memset(m, 0, sizeof(SignMem));
    m->priv[31] = 1;
    for (i = 0; i < 32u; i++) m->hash[i] = 0x55;
    rfc_setup(m);
    add_progress(WEIGHT_SIGN_RFC);
    if (!rfc_candidate(m)) {
        memset(m, 0, sizeof(SignMem));
        return 0;
    }
    memcpy(out, m->k, 32);
    memset(m, 0, sizeof(SignMem));
    return 1;
}

uint8_t ecdsa_bench_inv(uint8_t out[32]) BANKED {
    SignMem *m = borrow();
    if (!m) return 0;
    memset(m, 0, sizeof(SignMem));
    m->k[31] = 2;
    n_invmod(m, out, m->k);
    memset(m, 0, sizeof(SignMem));
    return 1;
}

uint8_t ecdsa_bench_mul(uint8_t out[32]) BANKED {
    SignMem *m = borrow();
    uint8_t i;
    if (!m) return 0;
    memset(m, 0, sizeof(SignMem));
    for (i = 0; i < 32u; i++) {
        m->r[i] = 0x11;
        m->priv[i] = 0x22;
    }
    n_mulmod(m, out, m->r, m->priv);
    memset(m, 0, sizeof(SignMem));
    return 1;
}

static const uint8_t VEC_TXID[32] = {
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11
};
static const uint8_t VEC_PAY[8] = {0x00,0x00,0x00,0x00,0x05,0xF5,0xE1,0x00};
static const uint8_t VEC_CHG[8] = {0x00,0x00,0x00,0x00,0x02,0xFA,0xF0,0x80};
static const uint8_t VEC_DEST[20] = {
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22
};
static const uint16_t VEC_VOUT = 1;

static void fill_vec(SignMem *m, SighashIn *in) {
    memset(m->h160, 0x44, 20);
    in->n_in = 1;
    in->has_change = 1;
    in->dest_type = 0;
    in->which = 0;
    in->txid = VEC_TXID;
    in->vout = &VEC_VOUT;
    in->pay = VEC_PAY;
    in->change = VEC_CHG;
    in->dest = VEC_DEST;
}

uint8_t ecdsa_bench_hash(uint8_t out[32]) BANKED {
    SignMem *m = borrow();
    SighashIn in;
    if (!m) return 0;
    memset(m, 0, sizeof(SignMem));
    fill_vec(m, &in);
    if (!sighash_one(m, &in)) {
        memset(m, 0, sizeof(SignMem));
        return 0;
    }
    memcpy(out, m->hash, 32);
    memset(m, 0, sizeof(SignMem));
    return 1;
}

uint8_t ecdsa_bench_sign(uint8_t out[106]) BANKED {
    SignMem *m = borrow();
    SighashIn in;
    if (!m) return 0;
    memset(m, 0, sizeof(SignMem));
    m->priv[31] = 1;
    fill_vec(m, &in);
    if (!sighash_one(m, &in) || !ecdsa_sign_mem(m)) {
        memset(m, 0, sizeof(SignMem));
        out[32] = 0;
        return 0;
    }
    memcpy(out, m->hash, 32);
    out[32] = m->sig_len;
    memcpy(out + 33, m->sig, m->sig_len);
    memset(m, 0, sizeof(SignMem));
    return 1;
}
