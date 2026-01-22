
#pragma bank 1

#include <gb/gb.h>
#include <bonktime.h>
#include "states.h"

extern AppState current_state;

void handle_bonk_game(void) BANKED {
    bonktime(BONKTIME_GAME_MODE);
    current_state = STATE_SLOT_SELECTION;
}
