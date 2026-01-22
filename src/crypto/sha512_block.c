#pragma bank 4
#include "sha512_block.h"
#include "common.h"
#include "endian.h"

/** Constants for SHA-512. See section 4.2.3 of FIPS PUB 180-4. */
static const uint64_t k[80] = {
0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc,
0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118,
0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694,
0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4,
0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70,
0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30,
0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8,
0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3,
0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b,
0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178,
0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c,
0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817};

// Optimized 64-bit rotation using 32-bit operations
static uint64_t ror64(uint64_t x, uint8_t n) {
    uint32_t lo = ((uint32_t*)&x)[0];
    uint32_t hi = ((uint32_t*)&x)[1];
    uint64_t res;
    if (n < 32) {
        ((uint32_t*)&res)[0] = (lo >> n) | (hi << (32 - n));
        ((uint32_t*)&res)[1] = (hi >> n) | (lo << (32 - n));
    } else {
        n -= 32;
        ((uint32_t*)&res)[0] = (hi >> n) | (lo << (32 - n));
        ((uint32_t*)&res)[1] = (lo >> n) | (hi << (32 - n));
    }
    return res;
}

// Optimized 64-bit shift using 32-bit operations
static uint64_t shr64(uint64_t x, uint8_t n) {
    uint32_t lo = ((uint32_t*)&x)[0];
    uint32_t hi = ((uint32_t*)&x)[1];
    uint64_t res;
    if (n < 32) {
        ((uint32_t*)&res)[0] = (lo >> n) | (hi << (32 - n));
        ((uint32_t*)&res)[1] = (hi >> n);
    } else {
        n -= 32;
        ((uint32_t*)&res)[0] = (hi >> n);
        ((uint32_t*)&res)[1] = 0;
    }
    return res;
}

// Optimized 64-bit addition using 32-bit operations
static uint64_t add64(uint64_t a, uint64_t b) {
    uint32_t lo = ((uint32_t*)&a)[0] + ((uint32_t*)&b)[0];
    uint32_t hi = ((uint32_t*)&a)[1] + ((uint32_t*)&b)[1];
    if (lo < ((uint32_t*)&a)[0]) hi++;
    uint64_t res;
    ((uint32_t*)&res)[0] = lo;
    ((uint32_t*)&res)[1] = hi;
    return res;
}

#define ROR64(x, n) ror64(x, n)
#define SHR64(x, n) shr64(x, n)
#define ADD64(a, b) add64(a, b)

// Optimized bitwise logic
#define ch(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define maj(x, y, z) (((x) & (y)) | ((z) & ((x) ^ (y))))

#define bigSigma0(x) (ROR64(x, 28) ^ ROR64(x, 34) ^ ROR64(x, 39))
#define bigSigma1(x) (ROR64(x, 14) ^ ROR64(x, 18) ^ ROR64(x, 41))
#define littleSigma0(x) (ROR64(x, 1) ^ ROR64(x, 8) ^ SHR64(x, 7))
#define littleSigma1(x) (ROR64(x, 19) ^ ROR64(x, 61) ^ SHR64(x, 6))

// Static buffer to avoid stack overflow
static uint64_t w[80];

/** Update hash value based on the contents of a full message buffer.
  * This implements the pseudo-code in section 6.4.2 of FIPS PUB 180-4.
  * \param hs64 The 64 bit hash state to update.
  */
void sha512Block(HashState64 *hs64) BANKED
{
	uint64_t a, b, c, d, e, f, g, h;
	uint64_t t1, t2;
	uint8_t t;

	for (t = 0; t < 16; t++)
	{
		w[t] = hs64->m[t];
	}
	for (t = 16; t < 80; t++)
	{
        // w[t] = littleSigma1(w[t - 2]) + w[t - 7] + littleSigma0(w[t - 15]) + w[t - 16];
		w[t] = ADD64(ADD64(ADD64(littleSigma1(w[t - 2]), w[t - 7]), littleSigma0(w[t - 15])), w[t - 16]);
	}
	a = hs64->h[0];
	b = hs64->h[1];
	c = hs64->h[2];
	d = hs64->h[3];
	e = hs64->h[4];
	f = hs64->h[5];
	g = hs64->h[6];
	h = hs64->h[7];
	for (t = 0; t < 80; t++)
	{
        // t1 = h + bigSigma1(e) + ch(e, f, g) + k[t] + w[t];
		t1 = ADD64(ADD64(ADD64(ADD64(h, bigSigma1(e)), ch(e, f, g)), k[t]), w[t]);
        // t2 = bigSigma0(a) + maj(a, b, c);
		t2 = ADD64(bigSigma0(a), maj(a, b, c));
		h = g;
		g = f;
		f = e;
        // e = d + t1;
		e = ADD64(d, t1);
		d = c;
		c = b;
		b = a;
        // a = t1 + t2;
		a = ADD64(t1, t2);
	}
    // hs64->h[0] += a;
	hs64->h[0] = ADD64(hs64->h[0], a);
	hs64->h[1] = ADD64(hs64->h[1], b);
	hs64->h[2] = ADD64(hs64->h[2], c);
	hs64->h[3] = ADD64(hs64->h[3], d);
	hs64->h[4] = ADD64(hs64->h[4], e);
	hs64->h[5] = ADD64(hs64->h[5], f);
	hs64->h[6] = ADD64(hs64->h[6], g);
	hs64->h[7] = ADD64(hs64->h[7], h);
}
