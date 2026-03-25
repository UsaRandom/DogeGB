
#pragma bank 1

#include <gb/gb.h>
#include <bonktime.h>
#include "mnemonic.h"
#include "src/assets/nophotos.h"
#include "states.h"

extern AppState current_state;
extern char words[12][9];

void handle_bonk_game(void) BANKED {
    bonktime(BONKTIME_GAME_MODE);
    current_state = STATE_SLOT_SELECTION;
}

void handle_bonktime_entropy(void) BANKED {

    uint8_t* entropy = bonktime(BONKTIME_ENTROPY_MODE);
    
    if(entropy == NULL) {
        current_state = STATE_GEN_TYPE_SELECTION;
        return;
    }
    set_bkg_palette(4, 1, nophotos_palettes);

    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 4, 20, 18, 4);
        VBK_REG = 0;
    }
    
    set_bkg_data(nophotos_TILE_ORIGIN, nophotos_TILE_COUNT, nophotos_tiles);
    set_bkg_tiles(3, 5, nophotos_WIDTH/8, nophotos_HEIGHT/8, nophotos_map);

    
    generate_mnemonic(words);

    current_state = STATE_SHOW_GENERATED_WORDS;
}
