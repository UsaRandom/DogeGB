#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/platform.h>
#include <gbdk/metasprites.h>
#include <gbdk/console.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "src/draw.h"
#include "src/assets/keyboard.h"
#include "src/assets/keyboard_lightgrey.h"
#include "src/crypto/bip39_wordlist.h"
#include "word_input.h"
#include "wallet.h"

#pragma bank 5

#define KEYBOARD_Y 12
#define MAX_LEN 8
#define BLANK_TILE 0x00
#define TYPING_Y 5
#define MESSAGE_Y 7

uint8_t attr_map[120];
extern uint8_t current_mode;

static uint16_t get_map_index(uint8_t mx, uint8_t my) {
    return (uint16_t)my * 20u + mx;
}

static uint8_t get_row(uint8_t s) {
    if (s < 10) return 0;
    if (s < 19) return 1;
    return 2;
}

static uint8_t find_closest_in_row(uint8_t row, uint8_t target_x) {
    uint8_t first = first_in_row[row];
    uint8_t last = last_in_row[row];
    uint8_t best = first;
    int16_t min_diff = abs((int16_t)keys[best].x - (int16_t)target_x);
    uint8_t best_x = keys[best].x;
    for (uint8_t i = first + 1; i <= last; i++) {
        int16_t diff = abs((int16_t)keys[i].x - (int16_t)target_x);
        uint8_t cur_x = keys[i].x;
        if (diff < min_diff || (diff == min_diff && cur_x > best_x)) {
            min_diff = diff;
            best_x = cur_x;
            best = i;
        }
    }
    return best;
}

static int get_key_index(char c) {
    c = toupper(c);
    if (c < 'A' || c > 'Z') return -1;
    for (int i = 0; i < 26; i++) { 
        if (keys[i].let == c) return i;
    }
    return -1;
}

static void set_attr(int s, uint8_t pal) {
    if (s < 0) return;
    uint8_t sx = keys[s].x;
    uint8_t sy = KEYBOARD_Y + keys[s].y;
    uint8_t rel_y = keys[s].y;
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tile_xy(sx, sy, pal);
        set_bkg_tile_xy(sx + 1, sy, pal);
        set_bkg_tile_xy(sx, sy + 1, pal);
        set_bkg_tile_xy(sx + 1, sy + 1, pal);
        VBK_REG = 0;
    } else {
        const uint8_t *map = (pal == 0) ? keyboard_lightgrey_map : keyboard_map;
        uint8_t tl = map[get_map_index(sx, rel_y)];
        uint8_t tr = map[get_map_index(sx + 1, rel_y)];
        uint8_t bl = map[get_map_index(sx, rel_y + 1)];
        uint8_t br = map[get_map_index(sx + 1, rel_y + 1)];
        set_bkg_tile_xy(sx, sy, tl);
        set_bkg_tile_xy(sx + 1, sy, tr);
        set_bkg_tile_xy(sx, sy + 1, bl);
        set_bkg_tile_xy(sx + 1, sy + 1, br);
    }
}

static void display_letter(int pos, int key_index, uint8_t pal) {
    uint8_t dx = 2 + pos * 2;
    uint8_t dy = TYPING_Y;
    uint8_t sx = keys[key_index].x;
    uint8_t sy = keys[key_index].y;
    const uint8_t *map;
    if (_cpu == CGB_TYPE) {
        map = keyboard_map;
    } else {
        map = (pal == 2) ? keyboard_lightgrey_map : keyboard_map;
    }
    uint8_t tl = map[get_map_index(sx, sy)];
    uint8_t tr = map[get_map_index(sx + 1, sy)];
    uint8_t bl = map[get_map_index(sx, sy + 1)];
    uint8_t br = map[get_map_index(sx + 1, sy + 1)];
    set_bkg_tile_xy(dx, dy, tl);
    set_bkg_tile_xy(dx + 1, dy, tr);
    set_bkg_tile_xy(dx, dy + 1, bl);
    set_bkg_tile_xy(dx + 1, dy + 1, br);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tile_xy(dx, dy, pal);
        set_bkg_tile_xy(dx + 1, dy, pal);
        set_bkg_tile_xy(dx, dy + 1, pal);
        set_bkg_tile_xy(dx + 1, dy + 1, pal);
        VBK_REG = 0;
    }
}

static void clear_letter(int pos) {
    uint8_t dx = 2 + pos * 2;
    uint8_t dy = TYPING_Y;
    set_bkg_tile_xy(dx, dy, BLANK_TILE);
    set_bkg_tile_xy(dx + 1, dy, BLANK_TILE);
    set_bkg_tile_xy(dx, dy + 1, BLANK_TILE);
    set_bkg_tile_xy(dx + 1, dy + 1, BLANK_TILE);
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_tile_xy(dx, dy, 0);
        set_bkg_tile_xy(dx + 1, dy, 0);
        set_bkg_tile_xy(dx, dy + 1, 0);
        set_bkg_tile_xy(dx + 1, dy + 1, 0);
        VBK_REG = 0;
    }
}

static void update_match(const char* prefix, char* matching_word, uint8_t* is_matched) {
    char lower_prefix[9];
    strcpy(lower_prefix, prefix);
    for (uint8_t i = 0; lower_prefix[i]; i++) {
        lower_prefix[i] = tolower(lower_prefix[i]);
    }
    char* found = find_unique_word(lower_prefix);
    if (found[0] != '\0' && strncmp(lower_prefix, found, strlen(lower_prefix)) == 0) {
        strcpy(matching_word, found);
        *is_matched = 1;
    } else {
        matching_word[0] = '\0';
        *is_matched = 0;
    }
}

static void refresh_typed_area(const char* text, const char* matching_word, uint8_t is_matched) {
    uint8_t user_len = strlen(text);

    for (uint8_t i = 0; i < user_len; i++) {
        int ki = get_key_index(text[i]);
        if (ki != -1) display_letter(i, ki, 0);
    }

    if (is_matched) {
        uint8_t full_len = strlen(matching_word);

        for (uint8_t i = user_len; i < full_len; i++) {
            int ki = get_key_index(matching_word[i]);
            if (ki != -1) display_letter(i, ki, 2);
        }

        for (uint8_t i = full_len; i < MAX_LEN; i++) {
            clear_letter(i);
        }

        gotoxy(0, MESSAGE_Y + 1);
        printf(" [Start] to Confirm ");
    } else {

        for (uint8_t i = user_len; i < MAX_LEN; i++) {
            clear_letter(i);
        }

        gotoxy(0, MESSAGE_Y + 1);
        printf("                    ");
    }
}

char* get_word_from_user(int wordNumber, const char* start) BANKED {
    static char result[9];
    result[0] = '\0';
    char text[9] = {0};
    uint8_t len = 0;
    char matching_word[9] = {0};
    uint8_t is_matched = 0;

    set_bkg_data(keyboard_lightgrey_TILE_ORIGIN, keyboard_lightgrey_TILE_COUNT, keyboard_lightgrey_tiles);
    set_bkg_data(keyboard_TILE_ORIGIN, keyboard_TILE_COUNT, keyboard_tiles);
    fill_bkg_rect(0, 0, 20, 18, BLANK_TILE);

    if (_cpu == CGB_TYPE) {
        set_bkg_tiles(0, KEYBOARD_Y, 20, 6, keyboard_map);
        memset(attr_map, 0, sizeof(attr_map));
        VBK_REG = 1;
        set_bkg_tiles(0, KEYBOARD_Y, 20, 6, attr_map);
        fill_bkg_rect(0, 0, 20, 18, 0);
        VBK_REG = 0;
    } else {
        set_bkg_tiles(0, KEYBOARD_Y, 20, 6, keyboard_lightgrey_map);
    }

    SHOW_BKG;
    DISPLAY_ON;

    char title[24];
    sprintf(title, "Enter word #%d:", wordNumber);

    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        fill_bkg_rect(0, 3, 20, 15, 0);
        VBK_REG = 0;
    }

    uint8_t title_len = strlen(title);

    if (title_len > 18) title_len = 18;

    draw_text(1, title, (20 - title_len) / 2);

    uint8_t sel = 12;

    switch(current_mode){
        case DOGEGB:
            sel = 12;
            break;
        case PEPEBG:
            sel = 9;
            break;
        case BELLSGB:
            sel = 23;
            break;
        default:
            break;
    }

    uint8_t prev_j = 0;
    int8_t prev_vertical_sel = -1;
    set_attr(sel, 1);
    
    if (start && start[0] != '\0') {
        strncpy(text, start, 8);
        text[8] = '\0';
        len = strlen(text);
        
        for (uint8_t i = 0; i < len; i++) {
            int ki = get_key_index(text[i]);
            if (ki != -1) {
                display_letter(i, ki, 0);
            }
        }
        strncpy(matching_word, start, 8);
        matching_word[8] = '\0';
        update_match(text, matching_word, &is_matched);
        refresh_typed_area(text, matching_word, is_matched);
        is_matched = 1;
    }
    while (1) {
        wait_vbl_done();
        uint8_t j = joypad();
        uint8_t new_press = j & ~prev_j;
        prev_j = j;
        uint8_t old_sel = sel;

        if (new_press & J_LEFT) {
            uint8_t r = get_row(sel);
            sel = (sel == first_in_row[r]) ? last_in_row[r] : sel - 1;
            prev_vertical_sel = -1;
        }
        if (new_press & J_RIGHT) {
            uint8_t r = get_row(sel);
            sel = (sel == last_in_row[r]) ? first_in_row[r] : sel + 1;
            prev_vertical_sel = -1;
        }
        if (new_press & J_UP) {
            uint8_t cur_row = get_row(sel);
            if (cur_row > 0) {
                uint8_t new_r = cur_row - 1;
                if (cur_row == 2) {
                    sel = find_closest_in_row(new_r, keys[sel].x);
                    prev_vertical_sel = -1;
                } else {
                    sel = (prev_vertical_sel != -1) ? prev_vertical_sel : find_closest_in_row(new_r, keys[sel].x);
                    prev_vertical_sel = -1;
                }
            }
        }
        if (new_press & J_DOWN) {
            uint8_t cur_row = get_row(sel);
            if (cur_row < 2) {
                uint8_t new_r = cur_row + 1;
                prev_vertical_sel = sel;
                sel = find_closest_in_row(new_r, keys[sel].x);
            }
        }
        if (old_sel != sel) {
            set_attr(old_sel, 0);
            set_attr(sel, 1);
        }
        if (new_press & J_A) {
            if (len < MAX_LEN) {
                text[len] = keys[sel].let;
                len++;
                text[len] = '\0';
                display_letter(len - 1, sel, 0);
                update_match(text, matching_word, &is_matched);
                refresh_typed_area(text, matching_word, is_matched);
            }
        }
        if (new_press & J_B) {
            if (len > 0) {
                len--;
                text[len] = '\0';
                clear_letter(len);
                update_match(text, matching_word, &is_matched);
                refresh_typed_area(text, matching_word, is_matched);
            } else {
                init_draw();
                return result;
            }
        }
        if (new_press & J_START) {
            if (is_matched && matching_word[0] != '\0') {
                strcpy(result, matching_word);
                init_draw();
                return result;
            }
        }
    }
}