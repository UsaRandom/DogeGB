/*

     New PIN

   No Palendromes

    O O O O - -

  [Select] to Undo
 [Start] to Confirm


  Blink & Scream
   Re-enter PIN

   
    O O O O - -

  [Select] to Undo/Cancel
 [Start] to Confirm


      Unlock

  Attempts Left: 9

    Attemps Left: 8
    --- or ---
     LAST ATTEMPT
  DATA WIPE IMMINENT

  Try Again in: 60s 
  
  [Select] to Undo
 [Start] to Confirm


*/


#include <gb/gb.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <draw.h>

#include "sha256.h"

#include "wallet_sram.h"

typedef enum {
  PIN_ENTER,
  PIN_SET,
  PIN_CONFIRM
} PinState;

uint8_t pin[6];

uint8_t* display_pin_entry_screen(PinState pin_state) {

    bool valid = false;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (save_magic == MAGIC) {
        valid = true;
    }
    DISABLE_RAM_MBC5;


    clear_screen();


    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        fill_bkg_rect(0, 3, 20, 17, 0); 
        VBK_REG = 0;
    }


    switch(pin_state) {
        case PIN_ENTER:
            draw_text(1, "Enter PIN:", 5);
            break;
        case PIN_SET:
            draw_text(1, "Set PIN:", 6);
            break;
        case PIN_CONFIRM:
            draw_text(1, "Confirm PIN:", 4);
            break;
        default:
            break;
    }

    draw_text(6, "No Palindromes", 3);

    draw_text(9, "- - -  - - -", 4);

    
    draw_text(11, "[Select] to Cancel", 1);
    draw_text(12, "[Start] to Confirm", 1);
    


}
