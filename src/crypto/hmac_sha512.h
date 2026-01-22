#ifndef HMAC_SHA512_H_INCLUDED
#define HMAC_SHA512_H_INCLUDED

#include "common.h"
#include <gb/gb.h>

/** Number of bytes a SHA-512 hash requires. */
#define SHA512_HASH_LENGTH		64

/** Container for 64 bit hash state. */
typedef struct HashState64Struct
{
	/** Where final hash value will be placed. */
	uint64_t h[8];
	/** Current index into HashState64#m, ranges from 0 to 15. */
	uint8_t index_m;
	/** Current byte within (64 bit) double word of HashState64#m. 0 = most
	  * significant byte, 7 = least significant byte. */
	uint8_t byte_position_m;
	/** 1024 bit message buffer. */
	uint64_t m[16];
	/** Total length of message; updated as bytes are written. */
	uint32_t message_length;
} HashState64;

void sha512Begin(HashState64 *hs64) BANKED;
void sha512WriteByte(HashState64 *hs64, const uint8_t byte) BANKED;
void sha512WriteBytes(HashState64 *hs64, const uint8_t *data, uint32_t len) BANKED;
void sha512Finish(uint8_t *out, HashState64 *hs64) BANKED;
void hmacSha512(uint8_t *out, const uint8_t *key, const unsigned int key_length, const uint8_t *text, const unsigned int text_length) BANKED;

#endif // #ifndef HMAC_SHA512_H_INCLUDED
