
#pragma bank 5

#include <gb/gb.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <draw.h>

#include "pin.h"

#include "wallet_sram.h"

extern uint8_t pin_hash[32];
extern uint8_t pin_double_hash[32];

uint8_t display_pin_entry_screen(PinState pin_state, uint8_t attempts_left, uint8_t* result) BANKED {


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
            gotoxy(0, 15);
            printf(" Tries Remaining:%d ", attempts_left);
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

    if(attempts_left == 1) {
        draw_text(5, "DATA WIPE IMMINENT", 1);
    }


    if(attempts_left < 5) {

        uint16_t wait_time = (10-attempts_left) * 10 * 60;

        while(wait_time) {

            uint16_t seconds_left = wait_time / 60;
            gotoxy(2, 9);
            printf("Try Again in %u   ", seconds_left);

      //      vsync_stir_entropy();
            wait_time--;
        }

    }


    uint8_t index = 0;
    uint8_t pin[6] = {0,0,0,0,0,0};
    uint8_t is_palendrome = 0;
    uint8_t last_input = 0;
    uint8_t input;

    while(1) {

        vsync_stir_entropy();


        if(pin_state == PIN_SET && index == 6) {
            if((pin[0] == pin[5]) && (pin[1] == pin[4]) && (pin[2] == pin[3])){
                draw_text(6, "No Palindromes", 3);
                is_palendrome = 1;
            }
            else {
                draw_text(6, "                    ", 0);
                is_palendrome = 0;
            }
        } else {
            draw_text(6, "                    ", 0);
        }

        if(index == 6 && !is_palendrome) { 
            draw_text(12, "[Start] to Confirm", 1);
        }
        else {
            draw_text(12, "                    ", 1);
        }

        char pin_str[16] = "- - -  - - -";
        for (int i = 0; i < 6; i++) {
            if (pin[i] != 0) {
                int pos = (i < 3) ? (i * 2) : (i * 2 + 1);
                pin_str[pos] = 'O';
            } else {
                break;
            }
        }
        draw_text(9, "    ", 0);
        draw_text(9, pin_str, 4);


        if(pin[0] != 0) {
            draw_text(11, " [Select] to Undo ", 1);
        }
        else if (pin_state != PIN_ENTER) {
            draw_text(11, "[Select] to Cancel", 1);
        }
        else {
            draw_text(11, "                    ", 1);
        }



        uint8_t current_keys = joypad();
        
        input = current_keys & ~last_input; 
        last_input = current_keys;
        
        if (input == 0) {
            continue;
        }

        if (input & (input - 1)) {
            continue; 
        }

        if(input & J_SELECT) {
            while(joypad()) {
                stir_entropy();
            }
            if(index == 0){
                if(pin_state == PIN_ENTER) {
                    continue;
                }
                return 0;
            }
            if(index == 0){
                continue;
            }
            pin[index-1] = 0;
            index--;
            continue;
        }

        if(input & J_START) {
            while(joypad()) {
                stir_entropy();
            }
            if(index == 6 && !is_palendrome) {
                memcpy(result, pin, 6);
                return 1;
            }
            continue;
        }

        if(index == 6) {
            continue;
        }

        pin[index] = input;

        index++;


    }

    
    

    return 0;
}
