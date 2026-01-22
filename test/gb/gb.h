// Stub Game Boy header for macOS testing
#ifndef GB_GB_H
#define GB_GB_H

// Include standard headers
#include <stdint.h>

// Basic type definitions
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;

// Stub out Game Boy functions
#define DISPLAY_OFF()
#define DISPLAY_ON()
#define HIDE_SPRITES()
#define SHOW_BKG()
#define SHOW_SPRITES()
#define HIDE_BKG()

// Stub out palette functions
#define set_bkg_palette(bank, count, palette)
#define set_sprite_palette(bank, count, palette)

// Stub out tile functions
#define set_bkg_data(start, count, data)
#define set_bkg_tiles(x, y, w, h, tiles)
#define set_sprite_data(start, count, data)
#define set_sprite_tile(index, tile)

// Stub out joypad
#define J_A 1
#define J_B 2
#define J_START 8
#define joypad() 0
#define waitpad(mask)
#define waitpadup()

// Stub out vsync
#define vsync()

// Stub out banking
#define CURRENT_BANK 0
#define SWITCH_ROM(bank) /* no-op */
#define BANKED
#define NONBANKED
#define BANK(name) 0
#define BANKREF(name)
#define BANKREF_EXTERN(name)

// Stub out memory
#define VBK_REG 0

#endif
