#include <gb/gb.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "wallet.h"
#include "wallet_sram.h"


uint8_t has_valid_save(void)  {
    uint8_t valid = FALSE;
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (save_magic1 == MAGIC1 && save_magic2 == MAGIC2) {
        valid = TRUE;
    }
    DISABLE_RAM_MBC5;
    return valid;
}

void save_wallet(uint8_t slot, 
                 const char* address, 
                 const char* pepeaddress, 
                 const char* bellsaddress, 
                 const char* mnemonic) 
{
    if (slot < 1 || slot > MAX_SLOTS) return;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    // Mark slot as used
    switch (slot) {
        case 1: slot1Used = SLOT_USED_MARKER; break;
        case 2: slot2Used = SLOT_USED_MARKER; break;
        case 3: slot3Used = SLOT_USED_MARKER; break;
        case 4: slot4Used = SLOT_USED_MARKER; break;
        case 5: slot5Used = SLOT_USED_MARKER; break;
        case 6: slot6Used = SLOT_USED_MARKER; break;
        case 7: slot7Used = SLOT_USED_MARKER; break;
        case 8: slot8Used = SLOT_USED_MARKER; break;
    }

    // Select target pointers based on slot
    char* target_addr  = NULL;
    char* target_pepe  = NULL;
    char* target_bells = NULL;
    char* target_mn    = NULL;

    switch (slot) {
        case 1:
            target_addr  = address1;
            target_pepe  = pepeaddress1;
            target_bells = bellsaddress1;
            target_mn    = mnemonic1;
            break;
        case 2:
            target_addr  = address2;
            target_pepe  = pepeaddress2;
            target_bells = bellsaddress2;
            target_mn    = mnemonic2;
            break;
        case 3:
            target_addr  = address3;
            target_pepe  = pepeaddress3;
            target_bells = bellsaddress3;
            target_mn    = mnemonic3;
            break;
        case 4:
            target_addr  = address4;
            target_pepe  = pepeaddress4;
            target_bells = bellsaddress4;
            target_mn    = mnemonic4;
            break;
        case 5:
            target_addr  = address5;
            target_pepe  = pepeaddress5;
            target_bells = bellsaddress5;
            target_mn    = mnemonic5;
            break;
        case 6:
            target_addr  = address6;
            target_pepe  = pepeaddress6;
            target_bells = bellsaddress6;
            target_mn    = mnemonic6;
            break;
        case 7:
            target_addr  = address7;
            target_pepe  = pepeaddress7;
            target_bells = bellsaddress7;
            target_mn    = mnemonic7;
            break;
        case 8:
            target_addr  = address8;
            target_pepe  = pepeaddress8;
            target_bells = bellsaddress8;
            target_mn    = mnemonic8;
            break;
    }

    if (target_addr) {
        strncpy(target_addr, address, ADDRESS_MAX_LEN);
        target_addr[ADDRESS_MAX_LEN] = '\0';
    }

    if (target_pepe) {
        strncpy(target_pepe, pepeaddress, ADDRESS_MAX_LEN);
        target_pepe[ADDRESS_MAX_LEN] = '\0';
    }

    if (target_bells) {
        strncpy(target_bells, bellsaddress, ADDRESS_MAX_LEN);
        target_bells[ADDRESS_MAX_LEN] = '\0';
    }

    if (target_mn) {
        strncpy(target_mn, mnemonic, MNEMONIC_MAX_LEN);
        target_mn[MNEMONIC_MAX_LEN] = '\0';
    }

    save_magic1 = MAGIC1;
    save_magic2 = MAGIC2;

    DISABLE_RAM_MBC5;
}

void get_wallet(uint8_t slot, uint8_t mode, wallet *out) {
    if (slot < 1 || slot > MAX_SLOTS) {
        out->slotNum = 255u;
        return;
    }

    char temp_addr[ADDRESS_MAX_LEN + 1] = {0};
    char temp_mn[MNEMONIC_MAX_LEN + 1]   = {0};
    uint16_t used_flag = 0;
    uint16_t magic1 = 0;
    uint16_t magic2 = 0;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    switch (slot) {
        case 1:
            used_flag = slot1Used;
            strncpy(temp_mn, mnemonic1, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress1,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress1, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address1,      ADDRESS_MAX_LEN);
            break;

        case 2:
            used_flag = slot2Used;
            strncpy(temp_mn, mnemonic2, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress2,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress2, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address2,      ADDRESS_MAX_LEN);
            break;

        case 3:
            used_flag = slot3Used;
            strncpy(temp_mn, mnemonic3, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress3,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress3, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address3,      ADDRESS_MAX_LEN);
            break;

        case 4:
            used_flag = slot4Used;
            strncpy(temp_mn, mnemonic4, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress4,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress4, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address4,      ADDRESS_MAX_LEN);
            break;

        case 5:
            used_flag = slot5Used;
            strncpy(temp_mn, mnemonic5, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress5,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress5, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address5,      ADDRESS_MAX_LEN);
            break;

        case 6:
            used_flag = slot6Used;
            strncpy(temp_mn, mnemonic6, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress6,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress6, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address6,      ADDRESS_MAX_LEN);
            break;

        case 7:
            used_flag = slot7Used;
            strncpy(temp_mn, mnemonic7, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress7,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress7, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address7,      ADDRESS_MAX_LEN);
            break;

        case 8:
            used_flag = slot8Used;
            strncpy(temp_mn, mnemonic8, MNEMONIC_MAX_LEN);
            temp_mn[MNEMONIC_MAX_LEN] = '\0';

            if (mode == PEPEBG)      strncpy(temp_addr, pepeaddress8,  ADDRESS_MAX_LEN);
            else if (mode == BELLSGB) strncpy(temp_addr, bellsaddress8, ADDRESS_MAX_LEN);
            else                      strncpy(temp_addr, address8,      ADDRESS_MAX_LEN);
            break;
    }

    magic1 = save_magic1;
    magic2 = save_magic2;

    DISABLE_RAM_MBC5;

    temp_addr[ADDRESS_MAX_LEN] = '\0';

    if (used_flag != SLOT_USED_MARKER ||
        magic1 != MAGIC1 ||
        magic2 != MAGIC2 ||
        temp_addr[0] == '\0') 
    {
        out->slotNum = 255u;
        out->address[0]  = '\0';
        out->mnemonic[0] = '\0';
        return;
    }

    out->slotNum = slot;
    strncpy(out->address,  temp_addr, ADDRESS_MAX_LEN + 1);
    strncpy(out->mnemonic, temp_mn,   MNEMONIC_MAX_LEN + 1);
}

void clear_slot(uint8_t slot) {
    if (slot < 1 || slot > MAX_SLOTS) return;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    switch (slot) {
        case 1:
            slot1Used = 0;
            memset(address1,     0, sizeof(address1));
            memset(pepeaddress1, 0, sizeof(pepeaddress1));
            memset(bellsaddress1,0, sizeof(bellsaddress1));
            memset(mnemonic1,    0, sizeof(mnemonic1));
            break;

        case 2:
            slot2Used = 0;
            memset(address2,     0, sizeof(address2));
            memset(pepeaddress2, 0, sizeof(pepeaddress2));
            memset(bellsaddress2,0, sizeof(bellsaddress2));
            memset(mnemonic2,    0, sizeof(mnemonic2));
            break;

        case 3:
            slot3Used = 0;
            memset(address3,     0, sizeof(address3));
            memset(pepeaddress3, 0, sizeof(pepeaddress3));
            memset(bellsaddress3,0, sizeof(bellsaddress3));
            memset(mnemonic3,    0, sizeof(mnemonic3));
            break;

        case 4:
            slot4Used = 0;
            memset(address4,     0, sizeof(address4));
            memset(pepeaddress4, 0, sizeof(pepeaddress4));
            memset(bellsaddress4,0, sizeof(bellsaddress4));
            memset(mnemonic4,    0, sizeof(mnemonic4));
            break;

        case 5:
            slot5Used = 0;
            memset(address5,     0, sizeof(address5));
            memset(pepeaddress5, 0, sizeof(pepeaddress5));
            memset(bellsaddress5,0, sizeof(bellsaddress5));
            memset(mnemonic5,    0, sizeof(mnemonic5));
            break;

        case 6:
            slot6Used = 0;
            memset(address6,     0, sizeof(address6));
            memset(pepeaddress6, 0, sizeof(pepeaddress6));
            memset(bellsaddress6,0, sizeof(bellsaddress6));
            memset(mnemonic6,    0, sizeof(mnemonic6));
            break;

        case 7:
            slot7Used = 0;
            memset(address7,     0, sizeof(address7));
            memset(pepeaddress7, 0, sizeof(pepeaddress7));
            memset(bellsaddress7,0, sizeof(bellsaddress7));
            memset(mnemonic7,    0, sizeof(mnemonic7));
            break;

        case 8:
            slot8Used = 0;
            memset(address8,     0, sizeof(address8));
            memset(pepeaddress8, 0, sizeof(pepeaddress8));
            memset(bellsaddress8,0, sizeof(bellsaddress8));
            memset(mnemonic8,    0, sizeof(mnemonic8));
            break;
    }

    DISABLE_RAM_MBC5;
}

void delete_mnemonic(uint8_t slot)  {
    if (slot < 1 || slot > MAX_SLOTS) return;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);

    switch (slot) {
        case 1: memset(mnemonic1, 0, sizeof(mnemonic1)); break;
        case 2: memset(mnemonic2, 0, sizeof(mnemonic2)); break;
        case 3: memset(mnemonic3, 0, sizeof(mnemonic3)); break;
        case 4: memset(mnemonic4, 0, sizeof(mnemonic4)); break;
        case 5: memset(mnemonic5, 0, sizeof(mnemonic5)); break;
        case 6: memset(mnemonic6, 0, sizeof(mnemonic6)); break;
        case 7: memset(mnemonic7, 0, sizeof(mnemonic7)); break;
        case 8: memset(mnemonic8, 0, sizeof(mnemonic8)); break;
    }

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

void list_slots(uint8_t mode, char slots[MAX_SLOTS][SLOT_DISPLAY_LEN])   {
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {

        wallet w;
        get_wallet(i + 1, mode, &w); 

        
        if (w.slotNum != 255u && w.address[0] != '\0') {
            format_address_display(w.address, slots[i], SLOT_DISPLAY_LEN);
        } else {
            strcpy(slots[i], "Empty Slot");
        }
    }
}