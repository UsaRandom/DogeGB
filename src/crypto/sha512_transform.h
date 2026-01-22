#ifndef SHA512_TRANSFORM_H
#define SHA512_TRANSFORM_H

#include "sha512.h"
#include <gb/gb.h>

void sha512_transform(SHA512_CTX *ctx, const uint8_t *data) BANKED;

#endif
