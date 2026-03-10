#include <gb/gb.h>
#include "wallet_sram.h"

uint16_t save_magic1;
uint16_t save_magic2;
SaveSlot slots[MAX_SLOTS];