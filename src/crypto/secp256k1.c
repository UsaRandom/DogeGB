#pragma bank 5
#include "secp256k1.h"
#include <string.h>
#include "progress.h"

// 256-bit big integer (32 bytes, big-endian)
typedef uint8_t bn256[32];

// secp256k1 prime p = 2^256 - 2^32 - 977
static const bn256 SECP256K1_P = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2F
};

// Generator point G.x
static const bn256 SECP256K1_GX = {
    0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
    0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
    0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
    0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
};

// Generator point G.y
static const bn256 SECP256K1_GY = {
    0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
    0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
    0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
    0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
};

// Static buffers for field arithmetic
static bn256 t1, t2, t3, t4;
static bn256 rx, ry, rz;  // Result point (Jacobian)
static bn256 px, py, pz;  // Temp point

// Compare a >= b
static int bn_cmp(const bn256 a, const bn256 b) {
    for (int i = 0; i < 32; i++) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

// a = b
static void bn_copy(bn256 a, const bn256 b) {
    for (int i = 0; i < 32; i++) a[i] = b[i];
}

// a = 0
static void bn_zero(bn256 a) {
    for (int i = 0; i < 32; i++) a[i] = 0;
}

// a = a + b (mod p)
static void bn_add_mod(bn256 a, const bn256 b) {
    uint16_t carry = 0;
    for (int i = 31; i >= 0; i--) {
        carry += a[i] + b[i];
        a[i] = carry & 0xFF;
        carry >>= 8;
    }
    if (carry || bn_cmp(a, SECP256K1_P) >= 0) {
        // Subtract p
        uint16_t borrow = 0;
        for (int i = 31; i >= 0; i--) {
            int16_t diff = a[i] - SECP256K1_P[i] - borrow;
            if (diff < 0) { diff += 256; borrow = 1; }
            else borrow = 0;
            a[i] = diff;
        }
    }
}

// a = a - b (mod p)
static void bn_sub_mod(bn256 a, const bn256 b) {
    int16_t borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int16_t diff = a[i] - b[i] - borrow;
        if (diff < 0) { diff += 256; borrow = 1; }
        else borrow = 0;
        a[i] = diff;
    }
    if (borrow) {
        // Add p back
        uint16_t carry = 0;
        for (int i = 31; i >= 0; i--) {
            carry += a[i] + SECP256K1_P[i];
            a[i] = carry & 0xFF;
            carry >>= 8;
        }
    }
}

// secp256k1 complement: c = 2^32 + 977 = 0x1000003d1
// In big-endian 5 bytes: {0x01, 0x00, 0x00, 0x03, 0xd1}
static const uint8_t SECP256K1_C[5] = {0x01, 0x00, 0x00, 0x03, 0xd1};

// Modular reduction for secp256k1
// p = 2^256 - c, so 2^256 ≡ c (mod p)
// For 512-bit product: result = low + high * c (mod p)
static void bn_reduce_secp256k1(uint8_t *prod, bn256 r) {
    static uint8_t full[64];  // Working buffer
    int i, j;
    uint32_t carry;
    
    // Copy product to working buffer
    for (i = 0; i < 64; i++) full[i] = prod[i];
    
    // Repeat reduction until high part is zero
    // Each iteration reduces by ~224 bits, so 3 iterations max
    for (int iter = 0; iter < 3; iter++) {
        // Check if high part (full[0..31]) is zero
        int high_zero = 1;
        for (i = 0; i < 32; i++) {
            if (full[i]) { high_zero = 0; break; }
        }
        if (high_zero) break;
        
        // Multiply high[0..31] by c[0..4] and add to low[32..63]
        // Result goes into a temp buffer, then we copy back
        static uint8_t temp[37];  // 32 + 5 = 37 bytes max
        memset(temp, 0, 37);
        
        // Schoolbook multiply: high * c
        for (i = 31; i >= 0; i--) {
            carry = 0;
            for (j = 4; j >= 0; j--) {
                // temp position: i + j + 1 maps to temp[0..36]
                // But we want high[i] * c[j] to go to position (31-i) + (4-j) from LSB
                // In big-endian: position = i + j + 1 relative to temp[0]
                int pos = i + j + 1;
                if (pos < 37) {
                    uint32_t v = temp[pos] + (uint32_t)full[i] * SECP256K1_C[j] + carry;
                    temp[pos] = v & 0xFF;
                    carry = v >> 8;
                }
            }
            // Propagate carry
            for (int k = i; k >= 0 && carry; k--) {
                uint32_t v = temp[k] + carry;
                temp[k] = v & 0xFF;
                carry = v >> 8;
            }
        }
        
        // Now add temp[5..36] to full[32..63] (the low part)
        // temp[0..4] is overflow that goes to full[27..31] area
        // Actually, temp represents high * c which is at most 256+40 = 296 bits
        // temp[0..36] in big-endian, we need to add this to full[32..63]
        // But temp is 37 bytes and full[32..63] is 32 bytes
        // temp[5..36] (32 bytes) aligns with full[32..63]
        // temp[0..4] (5 bytes) is overflow
        
        // Clear high part of full
        for (i = 0; i < 32; i++) full[i] = 0;
        
        // Add temp[5..36] to full[32..63]
        carry = 0;
        for (i = 36; i >= 5; i--) {
            uint32_t v = full[i + 32 - 5] + temp[i] + carry;
            full[i + 32 - 5] = v & 0xFF;
            carry = v >> 8;
        }
        
        // Add temp[0..4] (overflow) to full[27..31]
        for (i = 4; i >= 0; i--) {
            uint32_t v = full[27 + i] + temp[i] + carry;
            full[27 + i] = v & 0xFF;
            carry = v >> 8;
        }
        
        // Propagate any remaining carry
        for (i = 26; i >= 0 && carry; i--) {
            uint32_t v = full[i] + carry;
            full[i] = v & 0xFF;
            carry = v >> 8;
        }
    }
    
    // Copy low 256 bits to result
    for (i = 0; i < 32; i++) r[i] = full[32 + i];
    
    // Final reduction: subtract p while r >= p
    for (i = 0; i < 3; i++) {
        if (bn_cmp(r, SECP256K1_P) >= 0) {
            uint16_t borrow = 0;
            for (int j = 31; j >= 0; j--) {
                int16_t diff = r[j] - SECP256K1_P[j] - borrow;
                if (diff < 0) { diff += 256; borrow = 1; }
                else borrow = 0;
                r[j] = diff;
            }
        } else {
            break;
        }
    }
}

// r = a * b (mod p) - schoolbook multiplication with secp256k1 reduction
static void bn_mul_mod(bn256 r, const bn256 a, const bn256 b) {
    static uint8_t prod[64];
    int i, j;
    uint32_t carry;
    
    memset(prod, 0, 64);
    
    // Schoolbook multiply
    for (i = 31; i >= 0; i--) {
        carry = 0;
        for (j = 31; j >= 0; j--) {
            uint32_t sum = prod[i + j + 1] + (uint32_t)a[i] * b[j] + carry;
            prod[i + j + 1] = sum & 0xFF;
            carry = sum >> 8;
        }
        prod[i] += carry;
    }
    
    // Reduce mod p
    bn_reduce_secp256k1(prod, r);
}

// r = a^2 (mod p)
static void bn_sqr_mod(bn256 r, const bn256 a) {
    bn_mul_mod(r, a, a);
}

// r = a^(-1) (mod p) using Fermat's little theorem: a^(p-2) mod p
static void bn_inv_mod(bn256 r, const bn256 a) {
    static bn256 base, exp, result;
    bn_copy(base, a);
    
    // exp = p - 2
    bn_copy(exp, SECP256K1_P);
    exp[31] -= 2;
    
    // result = 1
    bn_zero(result);
    result[31] = 1;
    
    // Square-and-multiply
    for (int i = 0; i < 256; i++) {
        int byte_idx = 31 - (i / 8);
        int bit_idx = i % 8;
        if (exp[byte_idx] & (1 << bit_idx)) {
            bn_mul_mod(result, result, base);
        }
        bn_sqr_mod(base, base);
    }
    bn_copy(r, result);
}

// Check if point is at infinity (Z == 0)
static int is_infinity(void) {
    for (int i = 0; i < 32; i++) {
        if (pz[i] != 0) return 0;
    }
    return 1;
}

// Point doubling in Jacobian coordinates (a=0 for secp256k1)
// Using dbl-2009-l formula from https://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-0.html
// R = 2P where P = (px, py, pz)
static void point_double(void) {
    static bn256 A, B, C, D, E, F;
    
    if (is_infinity()) return; // Point at infinity
    
    // A = X1^2
    bn_sqr_mod(A, px);
    
    // B = Y1^2
    bn_sqr_mod(B, py);
    
    // C = B^2 = Y1^4
    bn_sqr_mod(C, B);
    
    // D = 2*((X1+B)^2 - A - C)
    bn_copy(D, px);
    bn_add_mod(D, B);           // D = X1 + B
    bn_sqr_mod(D, D);           // D = (X1 + B)^2
    bn_sub_mod(D, A);           // D = (X1 + B)^2 - A
    bn_sub_mod(D, C);           // D = (X1 + B)^2 - A - C
    bn_add_mod(D, D);           // D = 2*((X1+B)^2 - A - C)
    
    // E = 3*A = 3*X1^2
    bn_copy(E, A);
    bn_add_mod(E, A);
    bn_add_mod(E, A);           // E = 3*A
    
    // F = E^2
    bn_sqr_mod(F, E);
    
    // X3 = F - 2*D
    bn_copy(rx, F);
    bn_sub_mod(rx, D);
    bn_sub_mod(rx, D);          // X3 = F - 2*D
    
    // Y3 = E*(D - X3) - 8*C
    bn_copy(ry, D);
    bn_sub_mod(ry, rx);         // ry = D - X3
    bn_mul_mod(ry, E, ry);      // ry = E*(D - X3)
    bn_add_mod(C, C);           // C = 2*C
    bn_add_mod(C, C);           // C = 4*C
    bn_add_mod(C, C);           // C = 8*C
    bn_sub_mod(ry, C);          // Y3 = E*(D - X3) - 8*C
    
    // Z3 = 2*Y1*Z1
    bn_mul_mod(rz, py, pz);
    bn_add_mod(rz, rz);         // Z3 = 2*Y1*Z1
    
    bn_copy(px, rx);
    bn_copy(py, ry);
    bn_copy(pz, rz);
}

// Check if bn256 is zero
static int bn_is_zero(const bn256 a) {
    for (int i = 0; i < 32; i++) {
        if (a[i] != 0) return 0;
    }
    return 1;
}

// Point addition R = P + G (G is generator, with Z2=1)
// Using standard mixed Jacobian-affine addition
static void point_add_g(void) {
    static bn256 U2, S2, H, HH, HHH, r_val, V, tmp;
    
    // If P is infinity, R = G
    if (is_infinity()) {
        bn_copy(px, SECP256K1_GX);
        bn_copy(py, SECP256K1_GY);
        bn_zero(pz);
        pz[31] = 1;
        return;
    }
    
    // Mixed addition: P + G where G has Z=1
    // U1 = X1 (P.x in Jacobian is already "U1" conceptually)
    // U2 = Gx * Z1^2
    bn_sqr_mod(tmp, pz);              // tmp = Z1^2
    bn_mul_mod(U2, SECP256K1_GX, tmp); // U2 = Gx * Z1^2
    
    // S1 = Y1 (P.y in Jacobian is already "S1" conceptually)
    // S2 = Gy * Z1^3
    bn_mul_mod(S2, tmp, pz);          // S2 = Z1^3 (reusing S2 temporarily)
    bn_mul_mod(S2, SECP256K1_GY, S2); // S2 = Gy * Z1^3
    
    // H = U2 - U1 = U2 - px
    bn_copy(H, U2);
    bn_sub_mod(H, px);
    
    // r = S2 - S1 = S2 - py
    bn_copy(r_val, S2);
    bn_sub_mod(r_val, py);
    
    // If H == 0, points have same X coordinate
    if (bn_is_zero(H)) {
        if (bn_is_zero(r_val)) {
            // P == G, so P + G = 2P = 2G, use doubling
            point_double();
            return;
        } else {
            // P == -G, so P + G = infinity
            bn_zero(px);
            bn_zero(py);
            bn_zero(pz);
            return;
        }
    }
    
    // HH = H^2
    bn_sqr_mod(HH, H);
    
    // HHH = H^3
    bn_mul_mod(HHH, HH, H);
    
    // V = U1 * HH = px * HH
    bn_mul_mod(V, px, HH);
    
    // X3 = r^2 - HHH - 2*V
    bn_sqr_mod(rx, r_val);        // rx = r^2
    bn_sub_mod(rx, HHH);          // rx = r^2 - HHH
    bn_sub_mod(rx, V);            // rx = r^2 - HHH - V
    bn_sub_mod(rx, V);            // rx = r^2 - HHH - 2*V
    
    // Y3 = r*(V - X3) - S1*HHH = r*(V - X3) - py*HHH
    bn_copy(tmp, V);
    bn_sub_mod(tmp, rx);          // tmp = V - X3
    bn_mul_mod(ry, r_val, tmp);   // ry = r*(V - X3)
    bn_mul_mod(tmp, py, HHH);     // tmp = S1*HHH = py*HHH
    bn_sub_mod(ry, tmp);          // ry = r*(V - X3) - py*HHH
    
    // Z3 = Z1 * H
    bn_mul_mod(rz, pz, H);
    
    bn_copy(px, rx);
    bn_copy(py, ry);
    bn_copy(pz, rz);
}

// Convert Jacobian to affine: (X, Y, Z) -> (X/Z^2, Y/Z^3)
static void jacobian_to_affine(bn256 x, bn256 y) {
    bn_inv_mod(t1, pz);      // t1 = Z^(-1)
    bn_sqr_mod(t2, t1);      // t2 = Z^(-2)
    bn_mul_mod(x, px, t2);   // X = X * Z^(-2)
    bn_mul_mod(t3, t2, t1);  // t3 = Z^(-3)
    bn_mul_mod(y, py, t3);   // Y = Y * Z^(-3)
}

// Test function: compute Gx^2 mod p
void test_mul(uint8_t *result) BANKED {
    static bn256 gx_sq;
    bn_sqr_mod(gx_sq, SECP256K1_GX);
    for (int i = 0; i < 32; i++) result[i] = gx_sq[i];
}

// Public key generation: pubkey = privkey * G
void secp256k1_pubkey(const uint8_t *privkey, uint8_t *pubkey) BANKED {
    // Initialize result to point at infinity
    bn_zero(px);
    bn_zero(py);
    bn_zero(pz);
    
    // Double-and-add from MSB
    for (int i = 0; i < 256; i++) {
        point_double();
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        if (privkey[byte_idx] & (1 << bit_idx)) {
            point_add_g();
        }
        add_progress(WEIGHT_SECP256k1);
    }
    
    // Convert to affine coordinates
    static bn256 ax, ay;
    jacobian_to_affine(ax, ay);
    
    // Output compressed pubkey
    pubkey[0] = (ay[31] & 1) ? 0x03 : 0x02;
    memcpy(pubkey + 1, ax, 32);
}
