#include <gb/gb.h>
#include "wallet_sram.h"

uint64_t save_magic;
uint8_t has_pin;
uint8_t failed_pin_attempts;
uint8_t pass_double_hash[32];
uint8_t panic_pass_hash[32];
SaveSlot slots[MAX_SLOTS];