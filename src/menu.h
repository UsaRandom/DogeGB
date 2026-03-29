
#include <gb/gb.h>
#include <gbdk/far_ptr.h>

typedef enum {
    MENU_DEFAULT,
    MENU_DISPLAY_WORDS,
    MENU_NEW_WALLET_WORDS
} MenuMode;

int8_t show_backup_menu(int8_t startPos, uint8_t mode, char mnemonic[12][9]) BANKED;
int8_t show_menu(const char* title, const char** options, uint8_t num_options) BANKED;
void init_cursor_sprite(void) BANKED;