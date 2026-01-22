#pragma bank 2
#include "common.h"
#include "endian.h"
#include "hmac_sha512.h"
#include "sha512_block.h"

/** Clear the message buffer.
  * \param hs64 The 64 bit hash state to act on.
  */
static void clearM(HashState64 *hs64)
{
	hs64->index_m = 0;
	hs64->byte_position_m = 0;
	memset(hs64->m, 0, sizeof(hs64->m));
}

/** Begin calculating hash for new message.
  * See section 5.3.5 of FIPS PUB 180-4.
  * \param hs64 The 64 bit hash state to initialise.
  */
void sha512Begin(HashState64 *hs64) BANKED
{
	hs64->message_length = 0;
	hs64->h[0] = 0x6a09e667f3bcc908;
	hs64->h[1] = 0xbb67ae8584caa73b;
	hs64->h[2] = 0x3c6ef372fe94f82b;
	hs64->h[3] = 0xa54ff53a5f1d36f1;
	hs64->h[4] = 0x510e527fade682d1;
	hs64->h[5] = 0x9b05688c2b3e6c1f;
	hs64->h[6] = 0x1f83d9abfb41bd6b;
	hs64->h[7] = 0x5be0cd19137e2179;
	clearM(hs64);
}

/** Add one more byte to the message buffer and call sha512Block()
  * if the message buffer is full.
  * \param hs64 The 64 bit hash state to act on.
  * \param byte The byte to add.
  */
void sha512WriteByte(HashState64 *hs64, const uint8_t byte) BANKED
{
	hs64->message_length++;

    // Optimized write: avoid 64-bit math
    // Little Endian: MSB is at offset 7, LSB at 0 within the 8-byte word
    uint8_t *m_bytes = (uint8_t *)hs64->m;
    uint8_t offset = (hs64->index_m << 3) + (7 - hs64->byte_position_m);
    m_bytes[offset] = byte;

	if (hs64->byte_position_m == 7)
	{
		hs64->index_m++;
	}
	hs64->byte_position_m = (uint8_t)((hs64->byte_position_m + 1) & 7);
	if (hs64->index_m == 16)
	{
		sha512Block(hs64);
		clearM(hs64);
	}
}

void sha512WriteBytes(HashState64 *hs64, const uint8_t *data, uint32_t len) BANKED
{
    while (len--) {
        sha512WriteByte(hs64, *data++);
    }
}

/** Finalise the hashing of a message by writing appropriate padding and
  * length bytes, then write the hash value into a byte array.
  * \param out A byte array where the final SHA-512 hash value will be written
  *            into. This must have space for #SHA512_HASH_LENGTH bytes.
  * \param hs64 The 64 bit hash state to act on.
  */
void sha512Finish(uint8_t *out, HashState64 *hs64) BANKED
{
	uint32_t length_bits;
	uint8_t i;
	uint8_t buffer[16];

	// Subsequent calls to sha512WriteByte() will keep incrementing
	// message_length, so the calculation of length (in bits) must be
	// done before padding.
	length_bits = hs64->message_length << 3;

	// Pad using a 1 bit followed by enough 0 bits to get the message buffer
	// to exactly 896 bits full.
	sha512WriteByte(hs64, (uint8_t)0x80);
	while ((hs64->index_m != 14) || (hs64->byte_position_m != 0))
	{
		sha512WriteByte(hs64, 0);
	}
	// Write 128 bit length (in bits).
	memset(buffer, 0, 16);
	writeU32BigEndian(&(buffer[12]), length_bits);
	for (i = 0; i < 16; i++)
	{
		sha512WriteByte(hs64, buffer[i]);
	}
	for (i = 0; i < 8; i++)
	{
		writeU32BigEndian(&(out[i * 8]), (uint32_t)(hs64->h[i] >> 32));
		writeU32BigEndian(&(out[i * 8 + 4]), (uint32_t)hs64->h[i]);
	}
}

// Static buffers for hmacSha512 to avoid stack overflow
static uint8_t hmac_hash[SHA512_HASH_LENGTH];
static uint8_t hmac_padded_key[128];
static HashState64 hmac_hs64;

/** Calculate a 64 byte HMAC of an arbitrary message and key using SHA-512 as
  * the hash function.
  * The code in here is based on the description in section 5
  * ("HMAC SPECIFICATION") of FIPS PUB 198.
  * \param out A byte array where the HMAC-SHA512 hash value will be written.
  *            This must have space for #SHA512_HASH_LENGTH bytes.
  * \param key A byte array containing the key to use in the HMAC-SHA512
  *            calculation. The key can be of any length.
  * \param key_length The length, in bytes, of the key.
  * \param text A byte array containing the message to use in the HMAC-SHA512
  *             calculation. The message can be of any length.
  * \param text_length The length, in bytes, of the message.
  */
void hmacSha512(uint8_t *out, const uint8_t *key, const unsigned int key_length, const uint8_t *text, const unsigned int text_length) BANKED
{
	unsigned int i;

	// Determine key.
	memset(hmac_padded_key, 0, sizeof(hmac_padded_key));
	if (key_length <= sizeof(hmac_padded_key))
	{
		memcpy(hmac_padded_key, key, key_length);
	}
	else
	{
		sha512Begin(&hmac_hs64);
		for (i = 0; i < key_length; i++)
		{
			sha512WriteByte(&hmac_hs64, key[i]);
		}
		sha512Finish(hmac_padded_key, &hmac_hs64);
	}
	// Calculate hash = H((K_0 XOR ipad) || text).
	sha512Begin(&hmac_hs64);
	for (i = 0; i < sizeof(hmac_padded_key); i++)
	{
		sha512WriteByte(&hmac_hs64, (uint8_t)(hmac_padded_key[i] ^ 0x36));
	}
	for (i = 0; i < text_length; i++)
	{
		sha512WriteByte(&hmac_hs64, text[i]);
	}
	sha512Finish(hmac_hash, &hmac_hs64);
	// Calculate H((K_0 XOR opad) || hash).
	sha512Begin(&hmac_hs64);
	for (i = 0; i < sizeof(hmac_padded_key); i++)
	{
		sha512WriteByte(&hmac_hs64, (uint8_t)(hmac_padded_key[i] ^ 0x5c));
	}
	for (i = 0; i < sizeof(hmac_hash); i++)
	{
		sha512WriteByte(&hmac_hs64, hmac_hash[i]);
	}
	sha512Finish(out, &hmac_hs64);
}
