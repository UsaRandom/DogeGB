
#pragma bank 1

#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include "menu.h"
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

    const char* walletMenu[5];
    uint8_t choiceCount;

    if (hasMnemonic) {
        walletMenu[0] = "Receive (QR)";
        walletMenu[1] = "Sign TX (IR)";
        walletMenu[2] = "Display Words";
        walletMenu[3] = "Delete Words";
        walletMenu[4] = "Clear Slot";
        choiceCount = 5u;
    } else {
        walletMenu[0] = "Receive (QR)";
        walletMenu[1] = "Sign TX (IR)";
        walletMenu[2] = "Clear Slot";
        choiceCount = 3u;
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

            while(!joypad()) {stir_entropy();}
            init_draw();
        return;
    }

    if (selection == 1) {
        current_state = STATE_SIGN_TX_IR;
        return;
    }

    if (choiceCount == 5 && selection == 2) {
        char backup_words[12][9] = {{0}};
        const char* src = current_wallet.mnemonic;
        uint8_t i = 0;

        while (i < 12 && *src != '\0') {
            while (*src == ' ' || *src == '\t' || *src == '\n') src++;
            if (*src == '\0') break;

            uint8_t j = 0;
            while (*src && *src != ' ' && j < 8) {
                backup_words[i][j++] = *src++;
            }
            backup_words[i][j] = '\0';

            i++;
            while (*src == ' ') src++;
        }

        int8_t selection = show_backup_menu(0, MENU_DISPLAY_WORDS, backup_words);

    } else if (choiceCount == 5 && selection == 3) {
        const char* confirmOptions[] = { "No", "Yes" };
        int8_t confirm = show_menu("Delete Words?", confirmOptions, 2u);
        if (confirm == 1) {
            delete_mnemonic(current_wallet.slotNum);
            get_wallet(current_wallet.slotNum, current_mode, &current_wallet);
        }

    } else if ((choiceCount == 5 && selection == 4) ||
                (choiceCount == 3 && selection == 2)) {
        const char* confirmOptions[] = { "No", "Yes" };
        int8_t confirm = show_menu("Clear Slot?", confirmOptions, 2u);
        if (confirm == 1) {
            clear_slot(current_wallet.slotNum);
            memset(&current_wallet, 0, sizeof(current_wallet));
            current_state = STATE_SLOT_SELECTION;
        }
    }
}
