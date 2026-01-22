
#include <gb/gb.h>
#include <gbdk/far_ptr.h>

int8_t show_menu_with_start_pos(int8_t startPos, const char* title, const char** options, uint8_t num_options) BANKED;
int8_t show_menu(const char* title, const char** options, uint8_t num_options) BANKED;
