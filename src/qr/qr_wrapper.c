#include <gbdk/platform.h>
#include <gb/drawing.h>
#include <gb/gb.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#pragma bank 5
#include "src/qr/qrcodegen.h"

// See qrcodegen.h for setting the QR code version/capacity

#define SCALE 4

bool qr_generate(const char * embed_str, uint16_t len) NONBANKED {

    if (len > QR_MAX_PAYLOAD_BYTES) {
        return false;
    }

    uint8_t save_bank = CURRENT_BANK;
    SWITCH_ROM(BANK(qrcodegen));

    qrcodegen(embed_str, len);

    SWITCH_ROM(save_bank);

    return true;
}


void qr_render(void) NONBANKED
{
    uint8_t save_bank = CURRENT_BANK;
    SWITCH_ROM(BANK(qrcodegen));

    int16_t qr_width  = (int16_t)QR_FINAL_PIXEL_WIDTH  * SCALE;
    int16_t qr_height = (int16_t)QR_FINAL_PIXEL_HEIGHT * SCALE;

    uint8_t x_offset = (160 - qr_width)  / 2;
    uint8_t y_offset = (144 - qr_height) / 2;

    if (x_offset > 80) x_offset = 0;
    if (y_offset > 72) y_offset = 0;

    for (uint8_t x = 0; x < QR_FINAL_PIXEL_WIDTH; x++)
    {
        for (uint8_t y = 0; y < QR_FINAL_PIXEL_HEIGHT; y++)
        {
            uint8_t color_fg = qr(x,y) ? WHITE : BLACK;
            color(color_fg, color_fg, SOLID);

            uint8_t x1 = x_offset + x * SCALE;
            uint8_t y1 = y_offset + y * SCALE;

            box(x1, y1, x1 + (SCALE - 1), y1 + (SCALE - 1), M_FILL);
        }
    }

    SWITCH_ROM(save_bank);
}