// menu.c - Updated with start position support, no wrap-around navigation,
//          precise coin animation with 2-second idle on frame 0, skipping empty options
#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include "src/assets/dogecoin.h" 
#include "src/assets/pepecoin.h" 
#include "src/assets/bellscoin.h" 
#include "src/assets/chksum.h"
#include "bip39_wordlist.h"
#include <draw.h>
#include <wallet.h>
#include "menu.h"


#pragma bank 1

uint8_t cursor_pos = 0;
uint8_t target_pos = 0;
int8_t current_y_offset = 0;
uint8_t current_frame = 0;
uint8_t anim_counter = 0;
static const char* create_opt = "[Create Wallet]";
static uint8_t checksum_valid = 0;

extern const uint8_t current_mode;


#define CURSOR_SPRITE 0
#define CURSOR_Y_BASE 28
#define CURSOR_Y_STEP 8
#define OPTION_INDENT_X 3
#define SMOOTH_SPEED 2
#define FLIP_FRAME_DELAY 10
#define IDLE_HOLD_FRAMES 120

static const uint8_t coin_tiles[12] = {1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0};
static const uint8_t coin_flips[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


uint8_t menu_mode = MENU_DEFAULT;

void init_cursor_sprite(void) {

    switch(current_mode)
    {
        case DOGEGB:
            set_sprite_data(0, dogecoin_TILE_COUNT, dogecoin_tiles);
            set_sprite_palette(0, dogecoin_PALETTE_COUNT * dogecoin_COLORS_PER_PALETTE, dogecoin_palettes);
            break;
        case PEPEGB:
            set_sprite_data(0, pepecoin_TILE_COUNT, pepecoin_tiles);
            set_sprite_palette(0, pepecoin_PALETTE_COUNT * pepecoin_COLORS_PER_PALETTE, pepecoin_palettes);
            break;
        case BELLSGB:
            set_sprite_data(0, bellscoin_TILE_COUNT, bellscoin_tiles);
            set_sprite_palette(0, bellscoin_PALETTE_COUNT * bellscoin_COLORS_PER_PALETTE, bellscoin_palettes);
            break;
        default:
            break;
    }


    
    set_sprite_tile(CURSOR_SPRITE, 0);
    set_sprite_prop(CURSOR_SPRITE, 0);
    current_frame = 0;
    anim_counter = 0;
    
    SHOW_SPRITES;
}

void update_cursor_animation(void) {
    anim_counter++;
    
    if (current_frame == 0) {
        set_sprite_tile(CURSOR_SPRITE, 0);
        set_sprite_prop(CURSOR_SPRITE, 0);
        
        if (anim_counter >= IDLE_HOLD_FRAMES) {
            anim_counter = 0;
            current_frame = 1;
        }
    }
    else {
        if (anim_counter >= FLIP_FRAME_DELAY) {
            anim_counter = 0;
            current_frame++;
            
            if (current_frame > 12) {
                current_frame = 0;
            }
            else {
                uint8_t tile = coin_tiles[current_frame - 1];
                uint8_t flip = coin_flips[current_frame - 1] ? S_FLIPX : 0;
                
                set_sprite_tile(CURSOR_SPRITE, tile);
                set_sprite_prop(CURSOR_SPRITE, flip);
            }
        }
    }
}

void update_cursor_position(void) {
    uint8_t cursor_x = OPTION_INDENT_X * 8;
    uint8_t base_y = CURSOR_Y_BASE + (cursor_pos * CURSOR_Y_STEP) + 16;
    uint8_t y = base_y + current_y_offset + 4;
    move_sprite(CURSOR_SPRITE, cursor_x, y);
}

void hide_cursor_sprite(void) {
    move_sprite(CURSOR_SPRITE, 0, 0);
}

uint8_t next_valid_option(uint8_t idx, const char** options, uint8_t num_options) {
    for (uint8_t i = idx + 1; i < num_options; i++) {
        if (options[i][0] != '\0') {
            return i;
        }
    }
    return idx;
}

uint8_t prev_valid_option(uint8_t idx, const char** options, uint8_t num_options) {
    for (uint8_t i = idx; i-- > 0; ) {
        if (options[i][0] != '\0') {
            return i;
        }
    }
    return idx;
}

void update_cursor_target(void) {

    if (cursor_pos != target_pos) {
        int8_t diff = (int8_t)(target_pos - cursor_pos) * CURSOR_Y_STEP;
        if (diff > 0) {
            current_y_offset += SMOOTH_SPEED;
            if (current_y_offset > diff) current_y_offset = diff;
        } else if (diff < 0) {
            current_y_offset -= SMOOTH_SPEED;
            if (current_y_offset < diff) current_y_offset = diff;
        }
        if (current_y_offset == diff) {
            cursor_pos = target_pos;
            current_y_offset = 0;
        }
    }
    
}

void wait_no_buttons(void) {
    uint8_t timeout = 0;
    while (joypad() || timeout < 3) {
        update_cursor_target();
        update_cursor_animation();
        update_cursor_position();
        vsync_stir_entropy();
        if (!joypad()) timeout++;
    }
}

#define KONAMI_LENGTH 11
const uint8_t konami_code[] = { J_UP, J_UP, J_DOWN, J_DOWN, J_LEFT, J_RIGHT, J_LEFT, J_RIGHT, J_B, J_A, J_START };

int8_t menu(const char* title, const char** options, uint8_t num_options) BANKED {

    vsync();
    init_cursor_sprite();

    current_y_offset = 0;
    current_frame = 0;
    anim_counter = 0;
    
    VBK_REG = 1;
    fill_bkg_rect(0, 0, 20, 3, 1);
    fill_bkg_rect(0, 3, 20, 15, 0);
    VBK_REG = 0;
    
    fill_bkg_rect(0, 0, 20, 18, 0);

    if ((menu_mode == MENU_DISPLAY_WORDS || menu_mode == MENU_NEW_WALLET_WORDS) && !checksum_valid) {
        set_bkg_palette(6, 1, chksum_palettes);
        VBK_REG = 1;
        fill_bkg_rect(17, 0, 3, 3, 6);
        VBK_REG = 0;
        
        set_bkg_data(150, chksum_TILE_COUNT, chksum_tiles);
        set_bkg_tiles(17, 0, chksum_WIDTH/8, chksum_HEIGHT/8, chksum_map);
    }

    uint8_t title_len = strlen(title);
    if (title_len > 18) title_len = 18;
    uint8_t title_x = (20 - title_len) / 2;
    draw_text(1, title, title_x);
    
    uint8_t option_row = 4;
    for (uint8_t i = 0; i < num_options; i++) {
        if (options[i][0] != '\0') {
            draw_text(option_row + i, options[i], OPTION_INDENT_X);
        }
    }
    
    while (joypad()) stir_entropy();
    update_cursor_position();
    
    int8_t konami_index = 0;

    while (1) {
        uint8_t keys = joypad();

        if (keys & konami_code[konami_index]) {
            wait_no_buttons();
            konami_index++;
            if (konami_index == KONAMI_LENGTH) return -2;
        }
        else if (keys && !(konami_index == 2 && (keys & J_UP))){
            konami_index = 0;
        }
        

        if (keys & J_UP) {
            target_pos = prev_valid_option(target_pos, options, num_options);
            wait_no_buttons();
        }
        else if (keys & J_DOWN) {
            target_pos = next_valid_option(target_pos, options, num_options);
            wait_no_buttons();
        }
        else if (keys & (J_A | J_START) && konami_index == 0) {
            wait_no_buttons();
            hide_cursor_sprite();
            return target_pos;
        }
        else if (keys & J_B && konami_index == 0) {
            wait_no_buttons();
            hide_cursor_sprite();
            return -1;
        }
        
        update_cursor_target();
        update_cursor_animation();
        update_cursor_position();
        vsync_stir_entropy();
    }
}

int8_t show_backup_menu(int8_t startPos, uint8_t mode, char mnemonic[12][9]) BANKED {
    if (startPos < 0) startPos = 0;
    if (startPos > 11) startPos = 11;

    menu_mode = mode;

    uint8_t all_filled = 1;
    for (uint8_t i = 0; i < 12; i++) {
        if (mnemonic[i][0] == '\0') {
            all_filled = 0;
            break; 
        }
    }

    char mnemonic_str[12 * 9 + 13] = {0};
    char* dest = mnemonic_str;

    for (uint8_t i = 0; i < 12; i++) {
        if (i > 0) {
            *dest++ = ' ';
        }
        if (mnemonic[i][0]) {
            strcpy(dest, mnemonic[i]);
            dest += strlen(mnemonic[i]);
        }
    }
    *dest = '\0';

    static char numbered[13][20];
    const char* options[13];

    for (uint8_t i = 0; i < 12; i++) {
        if (mnemonic[i][0]) {
            sprintf(numbered[i], "%d. %s", i + 1, mnemonic[i]);
        } else {
            sprintf(numbered[i], "%d. ", i + 1);
        }
        options[i] = numbered[i];
    }

    uint8_t num_options = 12;

    if (mode == MENU_NEW_WALLET_WORDS && all_filled) {
        options[12] = create_opt;
        num_options = 13;
    }

    //only display checksum warning when all 12 words present
    checksum_valid = !all_filled || bip39_checksum_valid(mnemonic_str); 

    cursor_pos = startPos;
    target_pos = startPos;
    current_y_offset = 0;
    current_frame = 0;
    anim_counter = 0;

    const char* title = "Backup Words";
    return menu(title, options, num_options);
}

int8_t show_menu(const char* title, const char** options, uint8_t num_options) BANKED {

    menu_mode = MENU_DEFAULT;

    cursor_pos = 0;
    target_pos = 0;
    current_y_offset = 0;
    current_frame = 0;
    anim_counter = 0;
    
    return menu(title, options, num_options);
}