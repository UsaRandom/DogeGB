#ifndef ENDIAN_H_INCLUDED
#define ENDIAN_H_INCLUDED

#include "common.h"

void writeU32BigEndian(uint8_t *out, uint32_t in);
void writeU32LittleEndian(uint8_t *out, uint32_t in);
uint32_t readU32BigEndian(uint8_t *in);
uint32_t readU32LittleEndian(uint8_t *in);
void swapEndian(uint32_t *v);

#endif
