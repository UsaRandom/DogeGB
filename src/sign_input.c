#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "src/draw.h"
#include "src/assets/keyboard_base58.h"
#include "menu.h"
#include "sign_input.h"
#include "sign_review.h"

#pragma bank 13

/* Payment string from the web page. Every line is 8 characters. The 8th
   matches the mod-58 sum, or that sum plus 1..7 on a checksum line. */

#define VRAM_BASE 96u
#define SHEET_W 20u
#define KEY_N 36u
#define MAX_GROUPS 48u
#define GROUP_LEN 8u
#define GROUP_X0 1u
#define GROUP_X1 11u

#define ROW_LABEL 5u
#define ROW_CHARS 6u
/* 3x3 graphic in the title-band corners. Right edge is column 17. */
#define MARK_Y 0u
#define MARK_X0 0u
#define MARK_X1 17u

#define NAV_KEYS 0u
#define NAV_TEXT 1u

typedef struct Key {
    uint8_t x;
    uint8_t y;
    uint8_t sx;
    uint8_t sy;
    char ch;
    uint8_t row;
    uint8_t letter;
} Key;

/* Screen position, then the unshifted cell on the sheet. Letters flip
   to the matching lowercase cell eight tile-rows up. Shift is ch 0. */
static const Key keys[KEY_N] = {
    {0, 10, 0, 6, '1', 0, 0}, {2, 10, 2, 6, '2', 0, 0}, {4, 10, 4, 6, '3', 0, 0},
    {6, 10, 6, 6, '4', 0, 0}, {8, 10, 8, 6, '5', 0, 0}, {10, 10, 10, 6, '6', 0, 0},
    {12, 10, 12, 6, '7', 0, 0}, {14, 10, 14, 6, '8', 0, 0}, {16, 10, 16, 6, '9', 0, 0},
    {0, 12, 0, 8, 'Q', 1, 1}, {2, 12, 2, 8, 'W', 1, 1}, {4, 12, 4, 8, 'E', 1, 1},
    {6, 12, 6, 8, 'R', 1, 1}, {8, 12, 8, 8, 'T', 1, 1}, {10, 12, 10, 8, 'Y', 1, 1},
    {12, 12, 12, 8, 'U', 1, 1}, {14, 12, 14, 8, 'I', 1, 1}, {16, 12, 16, 8, 'O', 1, 1},
    {18, 12, 18, 8, 'P', 1, 1},
    {1, 14, 1, 10, 'A', 2, 1}, {3, 14, 3, 10, 'S', 2, 1}, {5, 14, 5, 10, 'D', 2, 1},
    {7, 14, 7, 10, 'F', 2, 1}, {9, 14, 9, 10, 'G', 2, 1}, {11, 14, 11, 10, 'H', 2, 1},
    {13, 14, 13, 10, 'J', 2, 1}, {15, 14, 15, 10, 'K', 2, 1}, {17, 14, 17, 10, 'L', 2, 1},
    {0, 16, 0, 4, 0, 3, 0},
    {3, 16, 3, 12, 'Z', 3, 1}, {5, 16, 5, 12, 'X', 3, 1}, {7, 16, 7, 12, 'C', 3, 1},
    {9, 16, 9, 12, 'V', 3, 1}, {11, 16, 11, 12, 'B', 3, 1}, {13, 16, 13, 12, 'N', 3, 1},
    {15, 16, 15, 12, 'M', 3, 1}
};

static const char ALPHA[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static char slots[MAX_GROUPS][GROUP_LEN];
static uint8_t shifted;
static uint8_t nav;
static uint8_t caret_group;
static uint8_t caret_col;
static uint8_t view_pair;
static uint8_t sel;
static uint8_t bad_group;

/* Uppercase I and O are not base58. Lowercase i and o are, so those keys
   show only while shifted. Lowercase l is not base58, so L hides then. */
static uint8_t key_hidden(uint8_t i) {
    char c = keys[i].ch;
    if (c == 'L') return shifted;
    if (c == 'I' || c == 'O') return !shifted;
    return 0u;
}

static uint8_t alpha_index(char c) {
    uint8_t i;
    for (i = 0; i < 58u; i++) {
        if (ALPHA[i] == c) return i;
    }
    return 0xFFu;
}

/* 0..7 when the 8th character is a legal check. 0xFF when the line is
   open or the check matches none of them. */
static uint8_t group_delta(uint8_t g) {
    uint16_t sum = 0;
    uint8_t i;
    uint8_t expect;
    uint8_t check;
    for (i = 0; i < GROUP_LEN; i++) {
        if (!slots[g][i]) return 0xFFu;
    }
    for (i = 0; i < 7u; i++) {
        uint8_t ix = alpha_index(slots[g][i]);
        if (ix == 0xFFu) return 0xFFu;
        sum += ix;
    }
    check = alpha_index(slots[g][7]);
    if (check == 0xFFu) return 0xFFu;
    expect = (uint8_t)(sum % 58u);
    return (uint8_t)((check + 58u - expect) % 58u);
}

static uint8_t buffer_empty(void) {
    uint8_t g, i;
    for (g = 0; g < MAX_GROUPS; g++) {
        for (i = 0; i < GROUP_LEN; i++) {
            if (slots[g][i]) return 0u;
        }
    }
    return 1u;
}

static uint8_t first_in_row(uint8_t row) {
    uint8_t i;
    for (i = 0; i < KEY_N; i++) {
        if (keys[i].row == row && !key_hidden(i)) return i;
    }
    return 0u;
}

static uint8_t last_in_row(uint8_t row) {
    uint8_t i;
    uint8_t last = 0;
    for (i = 0; i < KEY_N; i++) {
        if (keys[i].row == row && !key_hidden(i)) last = i;
    }
    return last;
}

static uint8_t closest_in_row(uint8_t row, uint8_t x) {
    uint8_t i;
    uint8_t best = first_in_row(row);
    uint8_t best_d = 0xFFu;
    for (i = 0; i < KEY_N; i++) {
        uint8_t d;
        if (keys[i].row != row || key_hidden(i)) continue;
        d = (keys[i].x > x) ? (uint8_t)(keys[i].x - x) : (uint8_t)(x - keys[i].x);
        if (d < best_d || (d == best_d && keys[i].x > keys[best].x)) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static void blit_sheet(uint8_t dx, uint8_t dy, uint8_t sx, uint8_t sy) {
    uint8_t r, c;
    for (r = 0; r < 2u; r++) {
        for (c = 0; c < 2u; c++) {
            uint8_t id = keyboard_base58_map[(uint16_t)(sy + r) * SHEET_W + (sx + c)];
            set_bkg_tile_xy(dx + c, dy + r, (uint8_t)(VRAM_BASE + id));
        }
    }
}

static void paint_key(uint8_t i) {
    uint8_t pal;
    uint8_t sy;
    const Key *k = &keys[i];
    if (key_hidden(i)) {
        set_bkg_tile_xy(k->x, k->y, 0);
        set_bkg_tile_xy(k->x + 1u, k->y, 0);
        set_bkg_tile_xy(k->x, k->y + 1u, 0);
        set_bkg_tile_xy(k->x + 1u, k->y + 1u, 0);
        if (_cpu == CGB_TYPE) {
            VBK_REG = 1;
            set_bkg_tile_xy(k->x, k->y, 0);
            set_bkg_tile_xy(k->x + 1u, k->y, 0);
            set_bkg_tile_xy(k->x, k->y + 1u, 0);
            set_bkg_tile_xy(k->x + 1u, k->y + 1u, 0);
            VBK_REG = 0;
        }
        return;
    }
    sy = k->sy;
    if (k->letter && shifted) sy = (uint8_t)(k->sy - 8u);
    blit_sheet(k->x, k->y, k->sx, sy);
    if (_cpu != CGB_TYPE) return;
    pal = 3u;
    if (nav == NAV_KEYS && i == sel) pal = 1u;
    else if (shifted && k->ch == 0) pal = 1u;
    pal |= 0x08u;
    VBK_REG = 1;
    set_bkg_tile_xy(k->x, k->y, pal);
    set_bkg_tile_xy(k->x + 1u, k->y, pal);
    set_bkg_tile_xy(k->x, k->y + 1u, pal);
    set_bkg_tile_xy(k->x + 1u, k->y + 1u, pal);
    VBK_REG = 0;
}

static void paint_keys(void) {
    uint8_t i;
    for (i = 0; i < KEY_N; i++) paint_key(i);
}

static void draw_slot(uint8_t x, uint8_t y, char c, uint8_t pal) {
    set_bkg_tile_xy(x, y, (uint8_t)((c ? c : '_') - 32));
    if (_cpu != CGB_TYPE) return;
    VBK_REG = 1;
    set_bkg_tile_xy(x, y, pal);
    VBK_REG = 0;
}

static void clear_mark(uint8_t x) {
    uint8_t r;
    uint8_t c;
    for (r = 0; r < 3u; r++) {
        for (c = 0; c < 3u; c++) set_bkg_tile_xy(x + c, MARK_Y + r, 0);
    }
    if (_cpu != CGB_TYPE) return;
    /* These cells sit in the inverted title band, so put that palette back. */
    VBK_REG = 1;
    for (r = 0; r < 3u; r++) {
        for (c = 0; c < 3u; c++) set_bkg_tile_xy(x + c, MARK_Y + r, 1);
    }
    VBK_REG = 0;
}

static void draw_mark(uint8_t x) {
    uint8_t r;
    uint8_t c;
    draw_chksum_mark(x, MARK_Y);
    if (_cpu != CGB_TYPE) return;
    VBK_REG = 1;
    for (r = 0; r < 3u; r++) {
        for (c = 0; c < 3u; c++) set_bkg_tile_xy(x + c, MARK_Y + r, 6);
    }
    VBK_REG = 0;
}

static void paint_text(void) {
    char lab[3];
    uint8_t i;
    uint8_t n;
    uint8_t g;

    clear_mark(MARK_X0);
    clear_mark(MARK_X1);

    n = (uint8_t)(view_pair + 1u);
    lab[0] = (char)('0' + n / 10u);
    lab[1] = (char)('0' + n % 10u);
    lab[2] = 0;
    draw_text(ROW_LABEL, lab, GROUP_X0 + 3u);
    n = (uint8_t)(view_pair + 2u);
    lab[0] = (char)('0' + n / 10u);
    lab[1] = (char)('0' + n % 10u);
    draw_text(ROW_LABEL, lab, GROUP_X1 + 3u);

    for (g = 0; g < 2u; g++) {
        uint8_t gi = (uint8_t)(view_pair + g);
        uint8_t x0 = g ? GROUP_X1 : GROUP_X0;
        uint8_t delta = group_delta(gi);
        for (i = 0; i < GROUP_LEN; i++) {
            uint8_t pal = (caret_group == gi && caret_col == i) ? 1u : 0u;
            draw_slot(x0 + i, ROW_CHARS, slots[gi][i], pal);
        }
        if (gi == bad_group || (delta != 0xFFu && delta > 7u))
            draw_mark(g ? MARK_X1 : MARK_X0);
    }
}

static void move_caret_left(void) {
    if (caret_col > 0) {
        caret_col--;
        return;
    }
    if (caret_group > 0) {
        caret_group--;
        caret_col = 7u;
        view_pair = (uint8_t)(caret_group & 0xFEu);
    }
}

static void move_caret_right(void) {
    if (caret_col + 1u < GROUP_LEN) {
        caret_col++;
        return;
    }
    if (caret_group + 1u < MAX_GROUPS) {
        caret_group++;
        caret_col = 0;
        view_pair = (uint8_t)(caret_group & 0xFEu);
    }
}

static char key_char(uint8_t i) {
    char c = keys[i].ch;
    if (!c) return 0;
    if (keys[i].letter && shifted) return (char)(c + ('a' - 'A'));
    return c;
}

static void toggle_shift(void) {
    shifted = !shifted;
    if (key_hidden(sel)) sel = closest_in_row(keys[sel].row, keys[sel].x);
    paint_keys();
}

static void present_keyboard(void) {
    char title[] = "Sign Tx";
    HIDE_SPRITES;
    clear_screen();
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        VBK_REG = 0;
    }
    draw_text(1, title, 6);
    paint_keys();
    paint_text();
}

/* 1 leaves Sign Tx for the wallet menu. Cancel comes back here. */
static uint8_t open_review(uint8_t count, uint8_t r) {
    uint8_t rev = sign_review_open(slots, count, r);
    if (rev == 2u) return 1u;
    if (!rev) bad_group = caret_group;
    else present_keyboard();
    paint_text();
    return 0u;
}

/* The first full group whose check is 1..7 ends the string. r is that
   check minus 1. r of 2..6 needs one more plain group, then the review.
   Returns 1 when the signature screens were left. */
static uint8_t after_full_group(void) {
    uint8_t d = group_delta(caret_group);
    uint8_t r;
    if (caret_group > 0) {
        uint8_t prev = group_delta((uint8_t)(caret_group - 1u));
        if (prev >= 1u && prev <= 7u) {
            r = (uint8_t)(prev - 1u);
            if (r >= 2u) {
                if (d != 0u) {
                    bad_group = caret_group;
                    paint_text();
                    return 0u;
                }
                return open_review((uint8_t)(caret_group + 1u), r);
            }
        }
    }
    if (d >= 1u && d <= 7u) {
        r = (uint8_t)(d - 1u);
        if (r >= 2u) {
            if (caret_group + 1u < MAX_GROUPS) {
                caret_group++;
                caret_col = 0;
                view_pair = (uint8_t)(caret_group & 0xFEu);
            }
            paint_text();
            return 0u;
        }
        return open_review((uint8_t)(caret_group + 1u), r);
    }
    if (d == 0u && caret_group + 1u < MAX_GROUPS) {
        caret_group++;
        caret_col = 0;
        view_pair = (uint8_t)(caret_group & 0xFEu);
    }
    paint_text();
    return 0u;
}

static uint8_t type_key(void) {
    char c = key_char(sel);
    if (!c) {
        toggle_shift();
        return 0u;
    }
    bad_group = 0xFFu;
    slots[caret_group][caret_col] = c;
    if (caret_col + 1u < GROUP_LEN) {
        caret_col++;
        paint_text();
        return 0u;
    }
    return after_full_group();
}

/* Step back onto the character just typed and erase it. A bad check
   on the last cell is erased in place, so the caret stays put for a
   new try. B on the first empty cell, with nothing typed, returns to
   the wallet menu. */
static uint8_t backspace(void) {
    uint8_t at_start = (caret_group == 0u && caret_col == 0u);
    uint8_t stay = 0;
    if (caret_col + 1u == GROUP_LEN && slots[caret_group][caret_col]) {
        uint8_t d = group_delta(caret_group);
        if (caret_group == bad_group || (d != 0xFFu && d > 7u)) stay = 1;
    }
    bad_group = 0xFFu;
    if (!at_start && !stay) move_caret_left();
    if (slots[caret_group][caret_col]) {
        slots[caret_group][caret_col] = 0;
        paint_text();
        return 0u;
    }
    if (at_start && buffer_empty()) return 1u;
    paint_text();
    return 0u;
}

static uint8_t step_row(uint8_t from, int8_t dir) {
    uint8_t row = keys[from].row;
    uint8_t i = from;
    for (;;) {
        if (dir < 0) {
            if (i == 0u || keys[i - 1u].row != row) return last_in_row(row);
            i--;
        } else {
            if (i + 1u >= KEY_N || keys[i + 1u].row != row) return first_in_row(row);
            i++;
        }
        if (!key_hidden(i)) return i;
    }
}

static void move_key(uint8_t np) {
    uint8_t old = sel;
    uint8_t row = keys[sel].row;
    if (np & J_LEFT) sel = step_row(sel, -1);
    if (np & J_RIGHT) sel = step_row(sel, 1);
    if (np & J_UP) {
        if (row == 0) {
            nav = NAV_TEXT;
            paint_key(old);
            paint_text();
            return;
        }
        sel = closest_in_row((uint8_t)(row - 1u), keys[old].x);
    }
    if (np & J_DOWN) {
        if (row < 3u) sel = closest_in_row((uint8_t)(row + 1u), keys[sel].x);
    }
    if (sel != old) {
        paint_key(old);
        paint_key(sel);
    }
}

void sign_tx_input(void) BANKED {
    uint8_t prev = 0;

    memset(slots, 0, sizeof(slots));
    shifted = 0;
    nav = NAV_KEYS;
    caret_group = 0;
    caret_col = 0;
    view_pair = 0;
    sel = 9;
    bad_group = 0xFFu;

    HIDE_SPRITES;
    clear_screen();
    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        set_bkg_data(VRAM_BASE, keyboard_base58_TILE_COUNT, keyboard_base58_tiles);
        VBK_REG = 0;
        set_bkg_palette(3, 1, keyboard_base58_palettes);
        load_chksum_gfx();
    } else {
        set_bkg_data(VRAM_BASE, keyboard_base58_TILE_COUNT, keyboard_base58_tiles);
    }
    present_keyboard();

    while (1) {
        uint8_t j;
        uint8_t np;
        wait_vbl_done();
        j = joypad();
        np = (uint8_t)(j & (uint8_t)~prev);
        prev = j;
        if (!np) continue;

        if (np & J_SELECT) toggle_shift();

        if (nav == NAV_KEYS) {
            move_key(np);
            if (nav == NAV_KEYS && (np & J_A) && type_key()) {
                init_draw();
                return;
            }
        } else {
            if (np & J_LEFT) {
                move_caret_left();
                paint_text();
            }
            if (np & J_RIGHT) {
                move_caret_right();
                paint_text();
            }
            if (np & (J_UP | J_DOWN)) {
                nav = NAV_KEYS;
                paint_key(sel);
                paint_text();
            }
        }
        if (np & J_B) {
            if (backspace()) {
                init_draw();
                return;
            }
        }
    }
}
