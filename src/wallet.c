#include <gb/gb.h>
#include <stdint.h>
#include <string.h>
#include "wallet.h"
#include "wallet_sram.h"

uint8_t has_valid_save(void) {
    uint8_t valid = FALSE;
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (save_magic == MAGIC) {
        valid = TRUE;
    }
    DISABLE_RAM_MBC5;
    return valid;
}

void save_wallet(uint8_t slot,
                 const char* address,
                 const char* pepeaddress,
                 const char* bellsaddress,
                 const char* mnemonic) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    uint8_t idx = slot - 1;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    slots[idx].used = SLOT_USED_MARKER;

    strncpy(slots[idx].address,      address,      ADDRESS_MAX_LEN);
    strncpy(slots[idx].pepeaddress,  pepeaddress,  ADDRESS_MAX_LEN);
    strncpy(slots[idx].bellsaddress, bellsaddress, ADDRESS_MAX_LEN);
    strncpy(slots[idx].mnemonic,     mnemonic,     MNEMONIC_MAX_LEN);

    slots[idx].address[ADDRESS_MAX_LEN]      = '\0';
    slots[idx].pepeaddress[ADDRESS_MAX_LEN]  = '\0';
    slots[idx].bellsaddress[ADDRESS_MAX_LEN] = '\0';
    slots[idx].mnemonic[MNEMONIC_MAX_LEN]    = '\0';

    save_magic = MAGIC;

    DISABLE_RAM_MBC5;
}

void get_wallet(uint8_t slot, uint8_t mode, wallet *out) {
    if (slot < 1 || slot > MAX_SLOTS) {
        out->slotNum = 255u;
        return;
    }
    uint8_t idx = slot - 1;

    char temp_addr[ADDRESS_MAX_LEN + 1] = {0};
    char temp_mn[MNEMONIC_MAX_LEN + 1] = {0};

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    uint16_t used_flag = slots[idx].used;
    strncpy(temp_mn, slots[idx].mnemonic, MNEMONIC_MAX_LEN);
    temp_mn[MNEMONIC_MAX_LEN] = '\0';

    if (mode == PEPEBG) {
        strncpy(temp_addr, slots[idx].pepeaddress, ADDRESS_MAX_LEN);
    } else if (mode == BELLSGB) {
        strncpy(temp_addr, slots[idx].bellsaddress, ADDRESS_MAX_LEN);
    } else {
        strncpy(temp_addr, slots[idx].address, ADDRESS_MAX_LEN);
    }
    temp_addr[ADDRESS_MAX_LEN] = '\0';

    uint64_t magic = save_magic;
    DISABLE_RAM_MBC5;

    if (used_flag != SLOT_USED_MARKER ||
        magic != MAGIC ||
        temp_addr[0] == '\0') {
        out->slotNum = 255u;
        out->address[0] = '\0';
        out->mnemonic[0] = '\0';
        return;
    }

    out->slotNum = slot;
    strncpy(out->address,  temp_addr, ADDRESS_MAX_LEN + 1);
    strncpy(out->mnemonic, temp_mn,   MNEMONIC_MAX_LEN + 1);
}

void clear_slot(uint8_t slot) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    uint8_t idx = slot - 1;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    memset(&slots[idx], 0, sizeof(SaveSlot));
    DISABLE_RAM_MBC5;
}

void delete_mnemonic(uint8_t slot) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    uint8_t idx = slot - 1;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    memset(slots[idx].mnemonic, 0, sizeof(slots[idx].mnemonic));
    DISABLE_RAM_MBC5;
}

void format_address_display(const char* full_address, char* output, uint8_t output_size) {
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

void list_slots(uint8_t mode, char slots_out[MAX_SLOTS][SLOT_DISPLAY_LEN]) {
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        wallet w;
        get_wallet(i + 1, mode, &w);
        if (w.slotNum != 255u && w.address[0] != '\0') {
            format_address_display(w.address, slots_out[i], SLOT_DISPLAY_LEN);
        } else {
            strcpy(slots_out[i], "Empty Slot");
        }
    }
}