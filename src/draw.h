#include <gb/cgb.h>
#include <stdio.h>
#include <string.h>
#include <gbdk/console.h>

void init_draw(void) BANKED;
void clear_screen(void) BANKED;
void draw_text(uint8_t y, const char* str, uint8_t start_x) NONBANKED;

extern const palette_color_t normal_palette[4];
extern const palette_color_t inverted_palette[4];
extern const palette_color_t grey_palette[4];