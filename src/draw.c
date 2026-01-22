#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdio.h>
#include <string.h>
#include <gbdk/console.h>

#include <gbdk/font.h>
#include <gbdk/platform.h>
#include <gbdk/metasprites.h>
#pragma bank 1



extern const palette_color_t normal_palette[4] = {
    RGB8(255, 255, 255), 
    RGB8(0, 0, 0),  
    RGB8(0,  0,  0),   
    RGB8(0,   0,   0)    
};
extern const palette_color_t inverted_palette[4] = {
    RGB8(0, 0, 0),    
    RGB8(255, 255, 255),   
    RGB8(255, 255, 255),  
    RGB8(255, 255, 255)  
};
extern const palette_color_t grey_palette[4] = {
    RGB8(255, 255, 255),
    RGB8(128, 128, 128),
    RGB8(128, 128, 128),
    RGB8(128, 128, 128)
};


static unsigned char zero_tile = 0x00;

void init_draw(void) BANKED {

    font_init();
    font_t font = font_load(font_ibm);
    font_set(font);

    set_bkg_palette(0, 1, normal_palette);
    set_bkg_palette(1, 1, inverted_palette);
    set_bkg_palette(2, 1, grey_palette);
}

void clear_screen(void) BANKED {
    fill_bkg_rect(0, 0, 20, 18, zero_tile);
    VBK_REG = 1;
    fill_bkg_rect(0, 0, 20, 18, 0);
    VBK_REG = 0;
    gotoxy(0, 0);
}

void draw_text(uint8_t y, const char* str, uint8_t start_x) BANKED {
    uint8_t len = strlen(str);
    if (len > 18) len = 18;
    
    unsigned char tiles[20];
    for (uint8_t i = 0; i < len; i++) {
        char c = str[i];
        tiles[i] = c - 32; 
    }
    
    VBK_REG = 0;
    set_bkg_tiles(start_x, y, len, 1, tiles);
}
