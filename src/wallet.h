#ifndef WALLET_H
#define WALLET_H

#include <gb/gb.h>
#include <stdint.h>

#define MAX_SLOTS              8u
#define SLOT_DISPLAY_LEN       16u

#define ADDRESS_MAX_LEN        35u
#define MNEMONIC_MAX_LEN       108u

enum WALLET_MODE {
    DOGEGB,
    BELLSGB,
    PEPEBG
};

typedef struct {
    uint8_t  slotNum;
    char     address[ADDRESS_MAX_LEN + 1];
    char     mnemonic[MNEMONIC_MAX_LEN + 1];
} wallet;

extern uint16_t save_magic1;
extern uint16_t save_magic2;

extern uint16_t slot1Used;
extern uint16_t slot2Used;
extern uint16_t slot3Used;
extern uint16_t slot4Used;
extern uint16_t slot5Used;
extern uint16_t slot6Used;
extern uint16_t slot7Used;
extern uint16_t slot8Used;

extern char address1[ADDRESS_MAX_LEN + 1];
extern char pepeaddress1[ADDRESS_MAX_LEN + 1];
extern char bellsaddress1[ADDRESS_MAX_LEN + 1];

extern char address2[ADDRESS_MAX_LEN + 1];
extern char pepeaddress2[ADDRESS_MAX_LEN + 1];
extern char bellsaddress2[ADDRESS_MAX_LEN + 1];

extern char address3[ADDRESS_MAX_LEN + 1];
extern char pepeaddress3[ADDRESS_MAX_LEN + 1];
extern char bellsaddress3[ADDRESS_MAX_LEN + 1];

extern char address4[ADDRESS_MAX_LEN + 1];
extern char pepeaddress4[ADDRESS_MAX_LEN + 1];
extern char bellsaddress4[ADDRESS_MAX_LEN + 1];

extern char address5[ADDRESS_MAX_LEN + 1];
extern char pepeaddress5[ADDRESS_MAX_LEN + 1];
extern char bellsaddress5[ADDRESS_MAX_LEN + 1];

extern char address6[ADDRESS_MAX_LEN + 1];
extern char pepeaddress6[ADDRESS_MAX_LEN + 1];
extern char bellsaddress6[ADDRESS_MAX_LEN + 1];

extern char address7[ADDRESS_MAX_LEN + 1];
extern char pepeaddress7[ADDRESS_MAX_LEN + 1];
extern char bellsaddress7[ADDRESS_MAX_LEN + 1];

extern char address8[ADDRESS_MAX_LEN + 1];
extern char pepeaddress8[ADDRESS_MAX_LEN + 1];
extern char bellsaddress8[ADDRESS_MAX_LEN + 1];

extern char mnemonic1[MNEMONIC_MAX_LEN + 1];
extern char mnemonic2[MNEMONIC_MAX_LEN + 1];
extern char mnemonic3[MNEMONIC_MAX_LEN + 1];
extern char mnemonic4[MNEMONIC_MAX_LEN + 1];
extern char mnemonic5[MNEMONIC_MAX_LEN + 1];
extern char mnemonic6[MNEMONIC_MAX_LEN + 1];
extern char mnemonic7[MNEMONIC_MAX_LEN + 1];
extern char mnemonic8[MNEMONIC_MAX_LEN + 1];

#define MAGIC1    0x4F44  
#define MAGIC2    0x4547

#define SLOT_USED_MARKER   4242u

void save_wallet(uint8_t slot, const char* address, const char* pepeaddress, const char* belladdress, const char* mnemonic) ;
void get_wallet(uint8_t slot, uint8_t mode, wallet *out);
void clear_slot(uint8_t slot) ;
void delete_mnemonic(uint8_t slot) ;
void list_slots(uint8_t mode, char slots[MAX_SLOTS][SLOT_DISPLAY_LEN]) ;
uint8_t has_valid_save(void) ;


#endif