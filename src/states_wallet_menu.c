
#pragma bank 1

#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include <menu.h>
#include <wallet.h>
#include "states.h"
#include "src/qr/qr_wrapper.h"
#include "draw.h"
#include "bitrot_save.h"



extern AppState current_state;
extern wallet current_wallet;
extern uint8_t current_slot;
extern uint8_t current_mode;

void format_addr(const char* full_address, char* output, uint8_t output_size) {
    if (full_address == NULL || full_address[0] == '\0' || output_size < 10) {
        strncpy(output, "Empty Slot", output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    size_t len = strlen(full_address);

    output[0] = '\0';

    if (len >= 9) {
        strncat(output, full_address, 5);

        strncat(output, "...", 3);

        if (len > 4) {
            strncat(output, full_address + len - 4, 4);
        } else {
            strncat(output, full_address, len);
        }
    } else {
        strncpy(output, full_address, output_size - 1);
    }

    output[output_size - 1] = '\0';
}



void handle_wallet_menu(void) BANKED {
    char menuTitle[32];
    if (strlen(current_wallet.address) >= 9u) {
        format_addr(current_wallet.address, menuTitle, 16);
    } else {
        strcpy(menuTitle, "Wallet Slot");
    }

    uint8_t hasMnemonic = (current_wallet.mnemonic[0] != '\0');

    const char* walletMenu[4];
    uint8_t choiceCount;

    if (hasMnemonic) {
        walletMenu[0] = "Receive (QR)";
        walletMenu[1] = "Display Words";
        walletMenu[2] = "Delete Words";
        walletMenu[3] = "Clear Slot";
        choiceCount = 4u;
    } else {
        walletMenu[0] = "Receive (QR)";
        walletMenu[1] = "Clear Slot";
        choiceCount = 2u;
    }

    int8_t selection = show_menu(menuTitle, walletMenu, choiceCount);

    if (selection < 0) {
        current_state = STATE_SLOT_SELECTION;
        return;
    }

    if (selection == 0) {

            VBK_REG = 1; fill_bkg_rect(0, 0, 20, 3, 0); VBK_REG = 0;
            cls();

            if(!validate_checksum(current_wallet.address)) {
                gotoxy(0,8);
                printf("   Corrupted Addr\n");
                while(!joypad()) {vsync();}
                current_state = STATE_WALLET_MENU;
                return;
            }

            qr_generate(current_wallet.address, 34);
            qr_render();

            while(!joypad()) {vsync();}
            init_draw();
        return;
    }
    

    if (choiceCount == 4 && selection == 1) {
        static const char* numberedWords[12];
        static char wordBuffers[12][16];
        uint8_t wordIndex = 0;
        char *src = current_wallet.mnemonic;

        while (wordIndex < 12 && *src != '\0') {
            char *dst = wordBuffers[wordIndex];
            
            uint8_t num = wordIndex + 1;
            if (num < 10) {
                *dst++ = '0' + num;
            } else {
                *dst++ = '0' + (num / 10);
                *dst++ = '0' + (num % 10);
            }
            *dst++ = '.';
            *dst++ = ' ';

            if(num < 10){
                *dst++ = ' ';
            }
            
            uint8_t chars = 0;
            while (*src != ' ' && *src != '\0' && chars < 12) {
                *dst++ = *src++;
                chars++;
            }
            *dst = '\0';
            
            numberedWords[wordIndex] = wordBuffers[wordIndex];
            wordIndex++;
            
            while (*src == ' ') src++;
        }
        show_menu("Backup Words", numberedWords, wordIndex);

    } else if (choiceCount == 4 && selection == 2) {
        const char* confirmOptions[] = { "No", "Yes" };
        int8_t confirm = show_menu("Delete Words?", confirmOptions, 2u);
        if (confirm == 1) {
            delete_mnemonic(current_wallet.slotNum);
            get_wallet(current_wallet.slotNum, current_mode, &current_wallet);
        }

    } else if ((choiceCount == 4 && selection == 3) ||
                (choiceCount == 2 && selection == 1)) {
        const char* confirmOptions[] = { "No", "Yes" };
        int8_t confirm = show_menu("Clear Slot?", confirmOptions, 2u);
        if (confirm == 1) {
            clear_slot(current_wallet.slotNum);
            memset(&current_wallet, 0, sizeof(current_wallet));
            current_state = STATE_SLOT_SELECTION;
        }
    }
}
