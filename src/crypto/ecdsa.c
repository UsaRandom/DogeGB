#pragma bank 5

#include "ecdsa.h"
#include "sha256.h"
#include "secp256k1.h"

#include <string.h>
#include <stdint.h>

// secp256k1 order N
static const uint8_t SECP256K1_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
    0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41
};

// N/2 for low-s normalization
static const uint8_t SECP256K1_N_HALF[32] = {
    0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x5D,0x57,0x6E,0x73,0x57,0xA4,0x50,0x1D,
    0xDF,0xE9,0x2F,0x46,0x68,0x1B,0x20,0xA0
};

// ---- mod-n 256-bit arithmetic ----

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

// a >>= 1 (in-place, big-endian)
static void n_shr1(uint8_t *a) {
    uint8_t carry = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t next = a[i] & 1;
        a[i] = (a[i] >> 1) | (carry << 7);
        carry = next;
    }
}

// out = a + b (no mod), returns carry
static uint8_t n_add(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint16_t carry = 0;
    for (int i = 31; i >= 0; i--) {
        carry += (uint16_t)a[i] + b[i];
        out[i] = carry & 0xFF;
        carry >>= 8;
    }
    return (uint8_t)carry;
}

// out = a - b, a >= b
static void n_sub(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint16_t borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int16_t d = (int16_t)a[i] - b[i] - borrow;
        if (d < 0) { d += 256; borrow = 1; } else borrow = 0;
        out[i] = (uint8_t)d;
    }
}

// out = (a + b) mod N
static void n_addmod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    uint8_t carry = n_add(out, a, b);
    if (carry || n_cmp(out, SECP256K1_N) >= 0)
        n_sub(out, out, SECP256K1_N);
}

// out = (a - b) mod N
static void n_submod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    if (n_cmp(a, b) >= 0) {
        n_sub(out, a, b);
    } else {
        uint8_t tmp[32];
        n_sub(tmp, b, a);
        n_sub(out, SECP256K1_N, tmp);
    }
}

// Reduce 512-bit value mod N (bit-by-bit, works on GBC)
// prod: 64 bytes big-endian input, out: 32 bytes
static void n_reduce512(uint8_t *out, const uint8_t *prod) {
    // We accumulate bit-by-bit from MSB
    // result = 0; for each bit of prod: result = (result * 2 + bit) mod N
    static uint8_t result[32];
    memset(result, 0, 32);
    for (int i = 0; i < 512; i++) {
        int byte_i = i / 8;
        int bit_i  = 7 - (i % 8);
        uint8_t bit = (prod[byte_i] >> bit_i) & 1;
        // result = (result * 2) mod N
        uint8_t carry = n_add(result, result, result);
        if (carry || n_cmp(result, SECP256K1_N) >= 0)
            n_sub(result, result, SECP256K1_N);
        // result += bit
        if (bit) {
            uint8_t one[32]; memset(one, 0, 32); one[31] = 1;
            n_addmod(result, result, one);
        }
    }
    n_copy(out, result);
}

// out = (a * b) mod N  (schoolbook 32x32 -> 64 then reduce)
static void n_mulmod(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    static uint8_t prod[64];
    memset(prod, 0, 64);
    for (int i = 31; i >= 0; i--) {
        uint32_t carry = 0;
        for (int j = 31; j >= 0; j--) {
            uint32_t s = prod[i+j+1] + (uint32_t)a[i]*b[j] + carry;
            prod[i+j+1] = s & 0xFF;
            carry = s >> 8;
        }
        prod[i] += (uint8_t)carry;
    }
    n_reduce512(out, prod);
}

// Halve x mod N: x = x/2 if x even, else (x+N)/2.
// Correctly handles the carry when x+N overflows 32 bytes.
static void n_half(uint8_t *x) {
    uint8_t carry = 0;
    if (x[31] & 1) {  // x is odd: add N to make it even
        carry = n_add(x, x, SECP256K1_N);
    }
    n_shr1(x);
    if (carry) x[0] |= 0x80;  // restore the overflow bit as MSB
    // May now be >= N if there was overflow; reduce once
    if (n_cmp(x, SECP256K1_N) >= 0) n_sub(x, x, SECP256K1_N);
}

// Binary extended GCD: out = a^(-1) mod N
static void n_invmod(uint8_t *out, const uint8_t *a) {
    static uint8_t u[32], v[32], A[32], C[32];
    n_copy(u, a);
    n_copy(v, SECP256K1_N);
    memset(A, 0, 32); A[31] = 1; // A = 1
    memset(C, 0, 32);              // C = 0

    while (!n_is_zero(u)) {
        while (!(u[31] & 1)) { // u is even
            n_shr1(u);
            n_half(A);
        }
        while (!(v[31] & 1)) { // v is even
            n_shr1(v);
            n_half(C);
        }
        if (n_cmp(u, v) >= 0) {
            n_sub(u, u, v);
            n_submod(A, A, C);
        } else {
            n_sub(v, v, u);
            n_submod(C, C, A);
        }
    }
    // v = gcd = 1, C = a^{-1} mod N
    n_copy(out, C);
}

// ---- HMAC-SHA256 (local, no HMAC-SHA256 in hmac.c) ----

static void hmac_sha256_local(uint8_t out[32],
                               const uint8_t *key, uint8_t key_len,
                               const uint8_t *data, uint16_t data_len) {
    static SHA256_CTX ctx;
    static uint8_t k_pad[64];
    static uint8_t inner[32];

    // key is always <= 32 bytes in RFC 6979 usage
    memset(k_pad, 0, 64);
    memcpy(k_pad, key, key_len);

    // ipad
    for (int i = 0; i < 64; i++) k_pad[i] ^= 0x36;
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    // restore k_pad and opad
    for (int i = 0; i < 64; i++) k_pad[i] ^= (0x36 ^ 0x5c);
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

// ---- RFC 6979 deterministic k ----

static void rfc6979_k(const uint8_t privkey[32], const uint8_t hash[32],
                      uint8_t k_out[32]) {
    static uint8_t V[32], K[32];
    static uint8_t msg[97]; // 0x00/0x01 + V(32) + 0x00 + priv(32) + hash(32)

    // Step b: V = 0x01 * 32
    memset(V, 0x01, 32);
    // Step c: K = 0x00 * 32
    memset(K, 0x00, 32);

    // Step d: K = HMAC_K(V || 0x00 || priv || hash)
    memcpy(msg,      V,       32);
    msg[32] = 0x00;
    memcpy(msg + 33, privkey, 32);
    memcpy(msg + 65, hash,    32);
    hmac_sha256_local(K, K, 32, msg, 97);

    // Step e: V = HMAC_K(V)
    hmac_sha256_local(V, K, 32, V, 32);

    // Step f: K = HMAC_K(V || 0x01 || priv || hash)
    memcpy(msg,      V,       32);
    msg[32] = 0x01;
    memcpy(msg + 33, privkey, 32);
    memcpy(msg + 65, hash,    32);
    hmac_sha256_local(K, K, 32, msg, 97);

    // Step g: V = HMAC_K(V)
    hmac_sha256_local(V, K, 32, V, 32);

    // Step h: generate k
    for (;;) {
        hmac_sha256_local(V, K, 32, V, 32); // V = HMAC_K(V)
        // Check: 0 < V < N
        if (!n_is_zero(V) && n_cmp(V, SECP256K1_N) < 0) {
            n_copy(k_out, V);
            return;
        }
        // retry
        msg[0] = 0x00;
        hmac_sha256_local(K, K, 32, msg, 1); // K = HMAC_K(V || 0x00) -- but msg has V already set after loop
        // rebuild msg for retry
        memcpy(msg, V, 32);
        msg[32] = 0x00;
        hmac_sha256_local(K, K, 32, msg, 33);
        hmac_sha256_local(V, K, 32, V, 32);
    }
}

// ---- DER encoding ----

static uint8_t der_encode(const uint8_t r[32], const uint8_t s[32],
                           uint8_t *out) {
    // Find minimal positive encoding for r and s
    uint8_t rbuf[33], sbuf[33];
    uint8_t rlen, slen;
    int ri = 0, si = 0;

    // Strip leading zeros
    while (ri < 31 && r[ri] == 0) ri++;
    while (si < 31 && s[si] == 0) si++;

    // Prepend 0x00 if high bit set
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

    uint8_t total = 4 + rlen + slen; // 0x02 rlen r 0x02 slen s
    uint8_t *p = out;
    *p++ = 0x30;
    *p++ = total;
    *p++ = 0x02;
    *p++ = rlen;
    memcpy(p, rbuf, rlen); p += rlen;
    *p++ = 0x02;
    *p++ = slen;
    memcpy(p, sbuf, slen); p += slen;
    *p++ = 0x01; // SIGHASH_ALL
    return (uint8_t)(p - out);
}

// ---- Public API ----

uint8_t ecdsa_sign(const uint8_t privkey[32], const uint8_t hash[32],
                   uint8_t sig_out[73]) BANKED {
    static uint8_t k[32], r[32], s[32];
    static uint8_t pubkey[33];
    static uint8_t k_inv[32];

    rfc6979_k(privkey, hash, k);

    // R = k*G, r = R.x mod N
    secp256k1_pubkey(k, pubkey); // reuse pubkey computation: k*G
    // pubkey[1..32] = R.x (compressed pubkey x-coordinate)
    memcpy(r, pubkey + 1, 32);
    if (n_cmp(r, SECP256K1_N) >= 0) n_sub(r, r, SECP256K1_N);

    // s = k^(-1) * (hash + r*privkey) mod N
    n_invmod(k_inv, k);
    n_mulmod(s, r, privkey);
    n_addmod(s, s, hash);
    n_mulmod(s, k_inv, s);

    // low-s normalization
    if (n_cmp(s, SECP256K1_N_HALF) > 0)
        n_sub(s, SECP256K1_N, s);

    return der_encode(r, s, sig_out);
}
