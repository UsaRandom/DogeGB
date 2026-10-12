#ifndef SIGN_REVIEW_H
#define SIGN_REVIEW_H

#include <gbdk/platform.h>

/* groups is count rows of 8 typed characters. Returns 1 after Cancel is
   confirmed, 2 when the signature screens are left for the wallet menu,
   3 when the string is from an older app, and 0 when a check fails. */
uint8_t sign_review_open(char groups[][8], uint8_t count) BANKED;

#endif
