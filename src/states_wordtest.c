#pragma bank 1

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
extern uint8_t test_streak;

static uint8_t last_test_word = 255;

void handle_confirm_generated_words(void) BANKED {
    if (test_streak >= 2) {
        current_state = STATE_GENERATE_ADDRESS;
        return;
    }

    uint8_t test_index = (DIV_REG + test_streak * 37u) % 12u;

    while(last_test_word == test_index){
        test_index = (DIV_REG + test_streak * 37u) % 12u;
    }
    
    last_test_word = test_index;

    char testBuffer[10][16];
    const char* testOptions[10];
    uint8_t correct_pos = (DIV_REG % 9u) + 1;

    uint8_t used[10] = {0};
    uint8_t placed = 0;

    strcpy(testBuffer[correct_pos], words[test_index]);
    testOptions[correct_pos] = testBuffer[correct_pos];
    used[correct_pos] = 1;
    placed++;

    while (placed < 10) {
        uint16_t rand_idx = ((uint16_t)DIV_REG << 8) | (LY_REG + placed);
        uint16_t word_idx = rand_idx % 2048u;

        get_bip39_word(word_idx, temp_buffer);

        uint8_t is_duplicate = 0;
        for (uint8_t i = 0; i < placed; i++) {
            if (strcmp(testOptions[i], temp_buffer) == 0) {
                is_duplicate = 1;
                break;
            }
        }

        if (!is_duplicate && strcmp(temp_buffer, words[test_index]) != 0) {
            uint8_t pos;
            do {
                pos = (DIV_REG + placed) % 10u;
            } while (used[pos]);

            strcpy(testBuffer[pos], temp_buffer);
            testOptions[pos] = testBuffer[pos];
            used[pos] = 1;
            placed++;
        }
    }

    char title[32];
    sprintf(title, "Enter Word #%u", test_index + 1u);

    int8_t selection = show_menu(title, testOptions, 10u);

    if (selection < 0) {
        test_streak = 0;
        current_state = STATE_SHOW_GENERATED_WORDS;
        return;
    }


    if ((uint8_t)selection == correct_pos) {
        test_streak++;
    } else {
        test_streak = 0;
        current_state = STATE_SHOW_GENERATED_WORDS;
    }
}