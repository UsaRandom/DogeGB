#ifndef WALLET_SRAM_H
#define WALLET_SRAM_H

#include <stdint.h>

#define MAX_SLOTS          8u
#define ADDRESS_MAX_LEN    35
#define MNEMONIC_MAX_LEN   108
#define SLOT_DISPLAY_LEN   16
#define PRIV_KEY_LEN       32
#define PUB_KEY_LEN        33

#define SLOT_USED_MARKER   1u
#define MAGIC              0x4E494F4345474F44ull //DOGECOIN

typedef struct {
    uint16_t used;
    char     address[ADDRESS_MAX_LEN + 1];
    char     pepeaddress[ADDRESS_MAX_LEN + 1];
    char     bellsaddress[ADDRESS_MAX_LEN + 1];
    char     mnemonic[MNEMONIC_MAX_LEN + 1];
    uint8_t  private_key[PRIV_KEY_LEN];
    uint8_t  public_key[PUB_KEY_LEN];
} SaveSlot;

extern uint64_t save_magic;
extern uint8_t has_pin;
extern uint8_t failed_pin_attempts;
extern uint8_t pass_double_hash[32];
extern uint8_t panic_pass_hash[32];
extern SaveSlot slots[MAX_SLOTS];

#endif