#include <gb/gb.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "wallet.h"
#include "wallet_sram.h"

extern uint8_t pin_hash[32];

// Consolidates 12 function calls into a single contiguous memory sweep
static void crypt_slot(SaveSlot *slot, const uint8_t *key) {
    uint8_t *data = (uint8_t *)slot->address;
    uint16_t len = sizeof(SaveSlot) - offsetof(SaveSlot, address);
    uint16_t lcg = (uint16_t)key[0] | ((uint16_t)key[1] << 8); 

    for (uint16_t i = 0; i < len; i++) {
        data[i] ^= key[(uint8_t)(lcg & 0x1F)];
        lcg = lcg * 5u + 1u;
    }
}

void update_hash(uint8_t new_hash[32], uint8_t new_double_hash[32], uint8_t new_panic_hash[32]) {
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].used == SLOT_USED_MARKER) {
            SaveSlot temp;
            memcpy(&temp, &slots[i], sizeof(SaveSlot));

            crypt_slot(&temp, pin_hash);
            crypt_slot(&temp, new_hash);

            memcpy(&slots[i], &temp, sizeof(SaveSlot));
        }
    }

    memcpy(pin_hash, new_hash, 32);
    memcpy(pass_double_hash, new_double_hash, 32);
    memcpy(panic_pass_hash, new_panic_hash, 32);

    DISABLE_RAM_MBC5;
}

uint8_t has_valid_save(void) {
    uint8_t valid = FALSE;
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (save_magic == MAGIC) valid = TRUE;
    DISABLE_RAM_MBC5;
    return valid;
}

void save_wallet(uint8_t slot,
                 const char* address,
                 const char* pepeaddress,
                 const char* bellsaddress,
                 const char* mnemonic,
                 const uint8_t privkey[32],
                 const uint8_t pubkey[33]) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    
    // memset compiles smaller than a {0} structural initializer
    SaveSlot temp;
    memset(&temp, 0, sizeof(SaveSlot)); 

    temp.used = SLOT_USED_MARKER;

    strncpy(temp.address,      address,      ADDRESS_MAX_LEN);
    strncpy(temp.pepeaddress,  pepeaddress,  ADDRESS_MAX_LEN);
    strncpy(temp.bellsaddress, bellsaddress, ADDRESS_MAX_LEN);
    strncpy(temp.mnemonic,     mnemonic,     MNEMONIC_MAX_LEN);
    memcpy(temp.private_key, privkey, 32);
    memcpy(temp.public_key,  pubkey, 33);

    crypt_slot(&temp, pin_hash); 

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    memcpy(&slots[slot - 1], &temp, sizeof(SaveSlot));
    save_magic = MAGIC;
    DISABLE_RAM_MBC5;
}

void get_wallet(uint8_t slot, uint8_t mode, wallet *out) {
    if (slot < 1 || slot > MAX_SLOTS) {
        out->slotNum = 255u;
        return;
    }

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    if (slots[slot - 1].used != SLOT_USED_MARKER || save_magic != MAGIC) {
        DISABLE_RAM_MBC5;
        out->slotNum = 255u;
        out->address[0] = '\0';
        out->mnemonic[0] = '\0';
        return;
    }

    // Pull whole slot to WRAM and decrypt in one pass
    SaveSlot temp;
    memcpy(&temp, &slots[slot - 1], sizeof(SaveSlot));
    DISABLE_RAM_MBC5;

    crypt_slot(&temp, pin_hash);

    char *target_addr = (mode == PEPEGB) ? temp.pepeaddress : 
                        (mode == BELLSGB) ? temp.bellsaddress : temp.address;

    if (target_addr[0] == '\0') {
        out->slotNum = 255u;
        out->address[0] = '\0';
        out->mnemonic[0] = '\0';
        return;
    }

    out->slotNum = slot;
    strncpy(out->address,  target_addr, ADDRESS_MAX_LEN + 1);
    strncpy(out->mnemonic, temp.mnemonic, MNEMONIC_MAX_LEN + 1);
}

void clear_slot(uint8_t slot) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    memset(&slots[slot - 1], 0, sizeof(SaveSlot));
    DISABLE_RAM_MBC5;
}

void delete_mnemonic(uint8_t slot) {
    if (slot < 1 || slot > MAX_SLOTS) return;
    uint8_t idx = slot - 1;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    if (slots[idx].used == SLOT_USED_MARKER) {
        SaveSlot temp;
        memcpy(&temp, &slots[idx], sizeof(SaveSlot));

        crypt_slot(&temp, pin_hash);
        memset(temp.mnemonic, 0, sizeof(temp.mnemonic));
        crypt_slot(&temp, pin_hash);

        memcpy(&slots[idx], &temp, sizeof(SaveSlot));
    } else {
        memset(slots[idx].mnemonic, 0, sizeof(slots[idx].mnemonic));
    }

    DISABLE_RAM_MBC5;
}

void format_address_display(const char* full_address, char* output, uint8_t output_size) {
    if (!full_address || !full_address[0] || output_size < 10) {
        strcpy(output, "Empty Slot");
        return;
    }
    
    size_t len = strlen(full_address);
    if (len >= 9) {
        // Direct memory offsets avoid the overhead of strncat loops
        memcpy(output, full_address, 5);
        memcpy(output + 5, "...", 3);
        memcpy(output + 8, full_address + len - 4, 4);
        output[12] = '\0';
    } else {
        strncpy(output, full_address, output_size - 1);
        output[output_size - 1] = '\0';
    }
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