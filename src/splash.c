#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdio.h>
#include <gbdk/console.h>

#include "src/assets/bork.h"

#include "bitrot_rom.h"


#pragma bank 5

static unsigned char blank_tile = 0;

const palette_color_t white[4] = {
    RGB8(255,255,255), RGB8(255,255,255),
    RGB8(255,255,255), RGB8(255,255,255)
};


void fade_palette(const palette_color_t* start_pal, const palette_color_t* target_pal) {

    if(_cpu == CGB_TYPE)
    {
        palette_color_t fade_pal[4];

        for (uint8_t step = 0; step <= 8; step++) {
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t start_r = (start_pal[i] >> 0)  & 0x1F;
                uint8_t start_g = (start_pal[i] >> 5)  & 0x1F;
                uint8_t start_b = (start_pal[i] >> 10) & 0x1F;

                uint8_t target_r = (target_pal[i] >> 0)  & 0x1F;
                uint8_t target_g = (target_pal[i] >> 5)  & 0x1F;
                uint8_t target_b = (target_pal[i] >> 10) & 0x1F;

                uint8_t r = start_r + ((target_r - start_r) * step / 8);
                uint8_t g = start_g + ((target_g - start_g) * step / 8);
                uint8_t b = start_b + ((target_b - start_b) * step / 8);

                fade_pal[i] = r | (g << 5) | (b << 10);
            }
            set_bkg_palette(0, 1, fade_pal);
            for (uint8_t f = 0; f < 6; f++) vsync();  
        }

        set_bkg_palette(0, 1, target_pal);
    }
    else {
        //OG gameboy has to fade from black otherwise it looks funky
        static const uint8_t black_to_image[] = {0xFF, 0xFE, 0xF9, 0xE4};
        static const uint8_t image_to_black[] = {0xE4, 0xF9, 0xFE, 0xFF};
        
        const uint8_t* steps = (start_pal == white) ? 
                               black_to_image : image_to_black;
        
        for (uint8_t i = 0; i < 4; i++) {
            BGP_REG  = steps[i];
            OBP0_REG = steps[i];
            OBP1_REG = steps[i];
            for (uint8_t f = 0; f < 12; f++) vsync();
        }
    }
   
}

void show_splash(
    uint16_t tile_origin,           
    const uint8_t* tiles,           
    uint16_t tile_count,            
    const uint8_t* map,             
    uint8_t map_width,             
    uint8_t map_height,            
    const palette_color_t* palette 
) BANKED {
    DISPLAY_OFF;
    HIDE_SPRITES;
    SHOW_BKG;


    fill_bkg_rect(0, 0, 20, 18, blank_tile);
    VBK_REG = 1;
    fill_bkg_rect(0, 0, 20, 18, 0);
    VBK_REG = 0;

    set_bkg_palette(0, 1, white);
    set_bkg_data(tile_origin, tile_count, tiles);

    uint8_t start_x = (20 - map_width)  / 2;
    uint8_t start_y = (18 - map_height) / 2;
    set_bkg_tiles(start_x, start_y, map_width, map_height, map);
    DISPLAY_ON;

    fade_palette(white, palette);

    if(!quick_rom_verify_integrity()){
        gotoxy(0,8);
        printf("   Corrupted ROM!\n");
        while(1) {
            vsync();
        }
    }

    fade_palette(palette, white);

    for (uint8_t y = 0; y < 18; y++) {
        for (uint8_t x = 0; x < 20; x++) {
            set_bkg_tiles(x, y, 1, 1, &blank_tile);
        }
    }


    if(_cpu != CGB_TYPE){
        BGP_REG = 0xE4;
        OBP0_REG = 0xE4;
        OBP1_REG = 0xE4;
    }

}

void show_doge_splash(void) BANKED {
    show_splash(
        bork_TILE_ORIGIN,                      
        bork_tiles,
        bork_TILE_COUNT,
        bork_map,
        bork_MAP_ATTRIBUTES_WIDTH,
        bork_MAP_ATTRIBUTES_HEIGHT,
        bork_palettes
    );
}
