#ifndef SHA512_BLOCK_H
#define SHA512_BLOCK_H

#include "hmac_sha512.h"
#include <gb/gb.h>

void sha512Block(HashState64 *hs64) BANKED;

#endif
