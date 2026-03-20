// src/states_generation.c
#pragma bank 4

#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include <menu.h>
#include <progress.h>
#include "src/crypto/mnemonic.h"
#include "src/crypto/hd_wallet.h"
#include "src/crypto/secp256k1.h"
#include "src/crypto/bip39_wordlist.h"
#include "states.h"
#include "word_input.h"
#include <bonktime.h>
#include <wallet.h>
#include "draw.h"

#include "bitrot_rom.h"
#include "bitrot_save.h"


#include <gbdk/console.h>

extern AppState current_state;
extern char words[12][9];
extern char temp_buffer[32];
extern unsigned char seed[64];
extern unsigned char privkey[32];
extern unsigned char pubkey[33];
extern char address[35];
extern uint8_t test_streak;

extern uint8_t current_mode;

extern uint8_t current_slot;
extern wallet current_wallet;

static int8_t wordSelection = 0;


void handle_bonktime_entropy(void) BANKED {
    uint8_t* entropy = bonktime(BONKTIME_ENTROPY_MODE);
    
    if(entropy == NULL) {
        current_state = STATE_GEN_TYPE_SELECTION;
        return;
    }
    
    fill_bkg_rect(0, 0, 20, 18, 0);

    char wait[19] = "Generating Words  \0";
    gotoxy(2,8);
    printf(wait);

    generate_mnemonic(words);

    current_state = STATE_SHOW_GENERATED_WORDS;
}

void handle_show_generated_words(void) BANKED {
    char title_backup[16];
    char create_opt[16];
    char title_cancel[8];
    char opt_no[4];
    char opt_yes[4];
    strcpy(title_backup, "Backup Words");
    strcpy(create_opt, "[Create Wallet]");
    strcpy(title_cancel, "Cancel?");
    strcpy(opt_no, "No");
    strcpy(opt_yes, "Yes");

    char numbered[12][16];
    const char* wordMenu[13];

    uint8_t all_filled = 1;
    for (uint8_t i = 0; i < 12; i++) {
        if (words[i][0] != '\0') {
            if(i >= 9){
                sprintf(numbered[i], "%u. %s", i + 1u, words[i]);
            }
            else {
                sprintf(numbered[i], "%u.  %s", i + 1u, words[i]);
            }
        } else {
            sprintf(numbered[i], "%u. ", i + 1u);
            all_filled = 0;
        }
        wordMenu[i] = numbered[i];
    }

    uint8_t menu_count = 12;
    if (all_filled) {
        wordMenu[12] = create_opt;
        menu_count = 13;
    }

    wordSelection = show_menu_with_start_pos(wordSelection, title_backup, wordMenu, menu_count);

    if (wordSelection < 0) {
        const char* confirmOptions[] = { opt_no, opt_yes};
        int8_t confirm = show_menu(title_cancel, confirmOptions, 2u);
        if (confirm == 1) {
            memset(words, 0, sizeof(words));
            current_state = STATE_SLOT_SELECTION;
        }
        wordSelection = 0;
        return;
    }

    if (all_filled && wordSelection == 12) {
        test_streak = 0;
        current_state = STATE_CONFIRM_GENERATED_WORDS;
        return;
    }

    if (wordSelection < 12) {
        char* newWord = get_word_from_user(wordSelection + 1, words[wordSelection]);

        if (newWord == NULL || newWord[0] == '\0') {
            words[wordSelection][0] = '\0';
        } else {
            strcpy(words[wordSelection], newWord);
        }
    }
}


void handle_generate_address(void) BANKED {
    char mnemonic_str[109] = {0};
   for (uint8_t i = 0; i < 12; i++) {
       if (i > 0) strcat(mnemonic_str, " ");
       strcat(mnemonic_str, words[i]);
   }

    show_progress_page();

    // perform full crc32 integrity check
    if(!rom_verify_integrity()){
        gotoxy(0,8);
        printf("   Corrupted ROM!\n");
        printf("                 \n");
        while(1) {
            vsync();
        }
    }

    uint8_t local_seed[64];
    uint8_t local_privkey[32];
    uint8_t local_pubkey[33];
    char    local_address[35];// = "DNQAyz6kPHUedoxpaUdHeXWPhgLa8bAFX4";
    char    local_pepeaddress[35];// = "PqBraorEqyXRu5b5DPHaFnar4o36fuBBVY";
    char    local_bellsaddress[35];// = "BMiA4ScJqPAGYeGPTNxhDu9TZA3rcdG7wg";

    mnemonic_to_seed(mnemonic_str, local_seed);

    #ifndef TEST_MODE
    __asm__("di");
    #endif

    seed_to_addresses(local_seed, local_address, local_pepeaddress, local_bellsaddress, local_privkey, local_pubkey);

    if(!validate_checksum(local_address) || !validate_checksum(local_pepeaddress) || !validate_checksum(local_bellsaddress)) {

        #ifndef TEST_MODE
        __asm__("ei");
        #endif

        gotoxy(0,8);
        printf("     !!FAILED!!   \n");
        printf("   Please Report!\n");
        while(1) {
            vsync();
        }
    }

    #ifndef TEST_MODE
    __asm__("ei");
    #endif

    switch(current_mode){
        case DOGEGB:
            strcpy(address, local_address);
        break;
        case PEPEGB:
            strcpy(address, local_pepeaddress);
            break;
        case BELLSGB:
            strcpy(address, local_bellsaddress);
        break;
        default:
        break;
    }

    #ifndef TEST_MODE
    __asm__("di");
    #endif

    save_wallet(current_slot, local_address, local_pepeaddress, local_bellsaddress, mnemonic_str, local_privkey, local_pubkey);

    #ifndef TEST_MODE
    __asm__("ei");
    #endif

    #ifndef TEST_MODE
    __asm__("di");
    #endif
    

    get_wallet(current_slot, current_mode, &current_wallet);

    #ifndef TEST_MODE
    __asm__("ei");
    #endif


    current_state = STATE_WALLET_MENU;
}

void handle_save_wallet(void) BANKED {

    current_state = STATE_SLOT_SELECTION;
}