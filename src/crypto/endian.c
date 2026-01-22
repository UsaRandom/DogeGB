#include "common.h"
#include "endian.h"

#pragma bank 1

void writeU32BigEndian(uint8_t *out, uint32_t in)
{
	out[0] = (uint8_t)(in >> 24);
	out[1] = (uint8_t)(in >> 16);
	out[2] = (uint8_t)(in >> 8);
	out[3] = (uint8_t)in;
}

void writeU32LittleEndian(uint8_t *out, uint32_t in)
{
	out[0] = (uint8_t)in;
	out[1] = (uint8_t)(in >> 8);
	out[2] = (uint8_t)(in >> 16);
	out[3] = (uint8_t)(in >> 24);
}

uint32_t readU32BigEndian(uint8_t *in)
{
	return ((uint32_t)in[0] << 24)
		| ((uint32_t)in[1] << 16)
		| ((uint32_t)in[2] << 8)
		| ((uint32_t)in[3]);
}

uint32_t readU32LittleEndian(uint8_t *in)
{
	return ((uint32_t)in[0])
		| ((uint32_t)in[1] << 8)
		| ((uint32_t)in[2] << 16)
		| ((uint32_t)in[3] << 24);
}

void swapEndian(uint32_t *v)
{
	uint8_t t;
	uint8_t *r;

	r = (uint8_t *)v;
	t = r[0];
	r[0] = r[3];
	r[3] = t;
	t = r[1];
	r[1] = r[2];
	r[2] = t;
}
