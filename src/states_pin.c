#pragma bank 1

#include <gb/gb.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <draw.h>

#include "states.h"
#include "menu.h"

#include "sha256.h"
#include "wallet_sram.h"

#include "pin.h"

#include "wallet.h"


extern AppState current_state;
extern uint8_t pin_set;
extern uint8_t pin_hash[32];
extern uint8_t pin_double_hash[32];


void short_wait() {
    for(uint8_t i = 0; i < 90; i++) {
        vsync_stir_entropy();
    }
}

void delete_everything() {
    for(uint8_t i = 1; i < 9; i++) {
        clear_slot(i);
    }
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    save_magic = 0;
    has_pin = 0;
    failed_pin_attempts = 0;
    memset(panic_pass_hash, 0, 32);
    memset(pass_double_hash, 0, 32);
    DISABLE_RAM_MBC5;
}

void get_hashes(uint8_t pin[6], uint8_t* out_hash, uint8_t* out_double_hash, uint8_t* out_panic_hash){
    gotoxy(0, 9);
    printf("    *Processing*    ");

    uint8_t painc_pin[6]; 

    painc_pin[0] = pin[5];
    painc_pin[1] = pin[4];
    painc_pin[2] = pin[3];
    painc_pin[3] = pin[2];
    painc_pin[4] = pin[1];
    painc_pin[5] = pin[0];

    SHA256_CTX ctx;
    uint8_t hash[32];
    uint8_t double_hash[32];
    uint8_t panic_hash[32];

    sha256_init(&ctx);
    sha256_update(&ctx, pin, 6);
    sha256_final(&ctx, hash);

    sha256_init(&ctx);
    sha256_update(&ctx, hash, 32);
    sha256_final(&ctx, double_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, painc_pin, 6);
    sha256_final(&ctx, panic_hash);

    memcpy(out_hash, hash, 32);
    memcpy(out_double_hash, double_hash, 32);
    memcpy(out_panic_hash, panic_hash, 32);
}

void printwait(char* str) {
    vsync_stir_entropy();
    clear_screen();
    gotoxy(0, 9);
    printf(str);
    short_wait();
}


void handle_enter_pin(void) BANKED {

    bool valid = false;
    uint8_t attempts_left = 10;
    uint8_t panic_hash[32];

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (save_magic == MAGIC) {
        valid = true;
        pin_set = has_pin;
        attempts_left = attempts_left - failed_pin_attempts;
        memcpy(pin_double_hash, pass_double_hash, 32);
        memcpy(panic_hash, panic_pass_hash, 32);
    }
    DISABLE_RAM_MBC5;

    if(!valid) {
        current_state = STATE_SET_PIN;
        return;
    }

    if(!pin_set){
        ENABLE_RAM_MBC5;
        SWITCH_RAM_MBC5(0);
        memcpy(pin_hash, panic_hash, 32);
        DISABLE_RAM_MBC5;
        current_state = STATE_SLOT_SELECTION;
        return;
    }

    uint8_t pin[6];

    while(1) {
        if(!display_pin_entry_screen(PIN_ENTER, attempts_left, pin)) {
            continue;
        }
        uint8_t temp_hash[32];
        uint8_t temp_double_hash[32];
        uint8_t temp_panic_hash[32];
        get_hashes(pin, temp_hash, temp_double_hash, temp_panic_hash);

        if(!memcmp(pin_double_hash, temp_double_hash, 32)) {
            ENABLE_RAM_MBC5;
            SWITCH_RAM_MBC5(0);
            failed_pin_attempts = 0;
            DISABLE_RAM_MBC5;
            memcpy(pin_hash, temp_hash, 32);
            current_state = STATE_SLOT_SELECTION;
            return;
        }

        if(!memcmp(panic_hash, temp_hash, 32)) {
            //silent panic, durress code entered.
            //delete data, set pin to panic code
            delete_everything();
            ENABLE_RAM_MBC5;
            SWITCH_RAM_MBC5(0);
            has_pin = 1;
            failed_pin_attempts = 0;
            save_magic = MAGIC;
            memcpy(pass_double_hash, temp_double_hash, 32);
            memcpy(panic_pass_hash, temp_panic_hash, 32);
            DISABLE_RAM_MBC5;
            memcpy(pin_hash, temp_hash, 32);
            current_state = STATE_SLOT_SELECTION;
            return;

        }

        //no match,

        attempts_left--;
        ENABLE_RAM_MBC5;
        SWITCH_RAM_MBC5(0);
        failed_pin_attempts++;
        DISABLE_RAM_MBC5;

        if(!attempts_left) {
            delete_everything();
            printwait("    DATA DELETED    ");
            current_state = STATE_SPLASH;
            return;
        }
    }




}

void handle_set_pin(void) BANKED {

    bool valid = false;
    uint8_t attempts_left = 10;
    uint8_t pin[6];
    uint8_t temp_hash[32];
    uint8_t temp_double_hash[32];
    uint8_t temp_panic_hash[32];
    int8_t answer = -1;
    
    uint8_t confirm_pin[6] = {0,0,0,0,0,0};

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (save_magic == MAGIC) {
        valid = true;
        pin_set = has_pin;
        attempts_left = attempts_left - failed_pin_attempts;
        memcpy(pin_double_hash, pass_double_hash, 32);
    }
    DISABLE_RAM_MBC5;

    

    if(!valid) {
        char* yesNo[] = {
            "Yes",
            "No"
        };

        //set all data to zero on first load
        //ensures unused wallet and panic wipe look the same.
        delete_everything();

        answer = show_menu("Create PIN?", yesNo, 2);

        //opt out of pin
        if(answer == 1) {

            get_hashes(confirm_pin, temp_hash, temp_double_hash, temp_panic_hash);

            ENABLE_RAM_MBC5;
            SWITCH_RAM_MBC5(0);
            save_magic = MAGIC;
            has_pin = 0;
            failed_pin_attempts = 0;
            DISABLE_RAM_MBC5;
            memcpy(pin_hash, temp_hash, 32);
            update_hash(temp_hash, temp_double_hash, temp_panic_hash);
            current_state = STATE_SLOT_SELECTION;
            return;
        }
    }

    if(pin_set){
        char* yesNoClear[] = {
            "[Update PIN]",
            "[Clear PIN]"
        };
        answer = show_menu("Update PIN?", yesNoClear, 2);

        if(answer == -1){
            current_state = STATE_SLOT_SELECTION;
            return;
        }

        if(!display_pin_entry_screen(PIN_CONFIRM, attempts_left, pin)) {
            current_state = STATE_SLOT_SELECTION;
            return;
        }

        get_hashes(pin, temp_hash, temp_double_hash, temp_panic_hash);

        if(memcmp(pin_double_hash, temp_double_hash, 32)) {
            ENABLE_RAM_MBC5;
            SWITCH_RAM_MBC5(0);
            failed_pin_attempts = 1;
            DISABLE_RAM_MBC5;
            printwait(" Incorrect! Locking ");
            current_state = STATE_ENTER_PIN;
            return;
        }

        if(answer == 1) {
            printwait("      Clearing      ");
            
            memset(confirm_pin, 0, 6);
            get_hashes(confirm_pin, temp_hash, temp_double_hash, temp_panic_hash);

            ENABLE_RAM_MBC5;
            SWITCH_RAM_MBC5(0);
            save_magic = MAGIC;
            has_pin = 0;
            failed_pin_attempts = 0;
            DISABLE_RAM_MBC5;

            update_hash(temp_hash, temp_double_hash, temp_panic_hash);
            memcpy(pin_hash, temp_hash, 32);

            printwait("    PIN Cleared!    ");
            current_state = STATE_SLOT_SELECTION;
            return;
        }
    }

    while(1) {
        memset(confirm_pin, 0, 6);
        if(!display_pin_entry_screen(PIN_SET, attempts_left, pin)){
            printwait("      Canceled      ");

            //first time run.
            if(!valid) {
                get_hashes(confirm_pin, temp_hash, temp_double_hash, temp_panic_hash);
                ENABLE_RAM_MBC5;
                SWITCH_RAM_MBC5(0);
                save_magic = MAGIC;
                has_pin = 0;
                failed_pin_attempts = 0;
                DISABLE_RAM_MBC5;
                memcpy(pin_hash, temp_hash, 32);
                update_hash(temp_hash, temp_double_hash, temp_panic_hash);
            }

            current_state = STATE_SLOT_SELECTION;
            return;
        }

        if(!display_pin_entry_screen(PIN_CONFIRM, attempts_left, confirm_pin)) {
            continue;
        }

        if(memcmp(pin, confirm_pin, 6)) {
            printwait("  PINs Don't Match  ");
            continue;
        }

        break;
    }
    
    get_hashes(confirm_pin, temp_hash, temp_double_hash, temp_panic_hash);



    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    save_magic = MAGIC;
    has_pin = 1;
    failed_pin_attempts = 0;
    DISABLE_RAM_MBC5;
    update_hash(temp_hash, temp_double_hash, temp_panic_hash);
    memcpy(pin_hash, temp_hash, 32);
    printwait("      PIN SET!      ");
    current_state = STATE_SLOT_SELECTION;
    return;
}