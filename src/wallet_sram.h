#ifndef WALLET_SRAM_H
#define WALLET_SRAM_H

#include <stdint.h>

#define MAX_SLOTS          8u
#define ADDRESS_MAX_LEN    35
#define MNEMONIC_MAX_LEN   108
#define SLOT_DISPLAY_LEN   16   // adjust if your list_slots call uses a different size

#define SLOT_USED_MARKER   1u
#define MAGIC1             0x4F44u
#define MAGIC2             0x4547u

typedef struct {
    uint16_t used;
    char     address[ADDRESS_MAX_LEN + 1];
    char     pepeaddress[ADDRESS_MAX_LEN + 1];
    char     bellsaddress[ADDRESS_MAX_LEN + 1];
    char     mnemonic[MNEMONIC_MAX_LEN + 1];
} SaveSlot;

extern uint16_t save_magic1;
extern uint16_t save_magic2;
extern SaveSlot slots[MAX_SLOTS];

#endif