#ifndef WALLET_H
#define WALLET_H

#include <gb/gb.h>
#include <stdint.h>
#include "wallet_sram.h"


enum WALLET_MODE {
    DOGEGB,
    BELLSGB,
    PEPEGB
};

typedef struct {
    uint8_t  slotNum;
    char     address[ADDRESS_MAX_LEN + 1];
    char     mnemonic[MNEMONIC_MAX_LEN + 1];
    uint8_t  private_key[PRIV_KEY_LEN];
    uint8_t  public_key[PUB_KEY_LEN];
} wallet;


void save_wallet(uint8_t slot,
                const char* address,
                const char* pepeaddress,
                const char* belladdress,
                const char* mnemonic,
                const uint8_t privkey[32],
                const uint8_t pubkey[33]) ;
void get_wallet(uint8_t slot, uint8_t mode, wallet *out);
void clear_slot(uint8_t slot) ;
void delete_mnemonic(uint8_t slot) ;
void list_slots(uint8_t mode, char slots[MAX_SLOTS][SLOT_DISPLAY_LEN]) ;
uint8_t has_valid_save(void) ;
void update_hash(uint8_t new_hash[32], uint8_t new_double_hash[32], uint8_t new_panic_hash[32]);


#endif