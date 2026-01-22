#include <gb/gb.h>
#include "wallet_sram.h"

#define SAVE_MAGIC1 0x4F44
#define SAVE_MAGIC2 0x4547

uint16_t save_magic1, save_magic2;

uint16_t slot1Used, slot2Used, slot3Used, slot4Used, slot5Used, slot6Used, slot7Used, slot8Used;

char address1[36];
char address2[36];
char address3[36];
char address4[36];
char address5[36];
char address6[36];
char address7[36];
char address8[36];

char pepeaddress1[36];
char pepeaddress2[36];
char pepeaddress3[36];
char pepeaddress4[36];
char pepeaddress5[36];
char pepeaddress6[36];
char pepeaddress7[36];
char pepeaddress8[36];

char bellsaddress1[36];
char bellsaddress2[36];
char bellsaddress3[36];
char bellsaddress4[36];
char bellsaddress5[36];
char bellsaddress6[36];
char bellsaddress7[36];
char bellsaddress8[36];

char mnemonic1[109];
char mnemonic2[109];
char mnemonic3[109];
char mnemonic4[109];
char mnemonic5[109];
char mnemonic6[109];
char mnemonic7[109];
char mnemonic8[109];