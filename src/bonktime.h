
#include <gb/gb.h>

#define BONKTIME_GAME_MODE 0
#define BONKTIME_ENTROPY_MODE 1

uint8_t* bonktime(uint8_t mode) BANKED;

/* 16x16 d-pad tiles in the free columns beside a version-3 QR. */
void draw_qr_sides(void) BANKED;