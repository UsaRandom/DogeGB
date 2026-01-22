
#ifndef WORD_INPUT_H
#define WORD_INPUT_H

#include <stdint.h>
#include <gbdk/platform.h>

char* get_word_from_user(int wordNumber, const char* start) BANKED;

typedef struct Key {
    uint8_t x;
    uint8_t y;
    char let;
} Key;

static const Key keys[26] = {
    {0,0,'Q'}, {2,0,'W'}, {4,0,'E'}, {6,0,'R'}, {8,0,'T'}, {10,0,'Y'}, {12,0,'U'}, {14,0,'I'}, {16,0,'O'}, {18,0,'P'},
    {1,2,'A'}, {3,2,'S'}, {5,2,'D'}, {7,2,'F'}, {9,2,'G'}, {11,2,'H'}, {13,2,'J'}, {15,2,'K'}, {17,2,'L'},
    {3,4,'Z'}, {5,4,'X'}, {7,4,'C'}, {9,4,'V'}, {11,4,'B'}, {13,4,'N'}, {15,4,'M'}
};

static const uint8_t first_in_row[3] = {0, 10, 19};
static const uint8_t last_in_row[3]  = {9, 18, 25};

#endif