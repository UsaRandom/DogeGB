#ifndef SIGN_REVIEW_H
#define SIGN_REVIEW_H

#include <gbdk/platform.h>

/* groups is count rows of 8 typed characters. r is the leftover count
   from the end group's check, 0..6. Returns 1 after Cancel is confirmed,
   2 when the signature screens are left for the wallet menu, and 0 when
   the end check does not match. */
uint8_t sign_review_open(char groups[][8], uint8_t count, uint8_t r) BANKED;

#endif
