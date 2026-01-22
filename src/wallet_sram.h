#ifndef WALLET_SRAM_H
#define WALLET_SRAM_H

#include <gb/gb.h>

extern uint16_t save_magic1, save_magic2;

extern uint16_t slot1Used, slot2Used, slot3Used, slot4Used, slot5Used, slot6Used, slot7Used, slot8Used;

extern char address1[36];
extern char address2[36];
extern char address3[36];
extern char address4[36];
extern char address5[36];
extern char address6[36];
extern char address7[36];
extern char address8[36];


extern char pepeaddress1[36];
extern char pepeaddress2[36];
extern char pepeaddress3[36];
extern char pepeaddress4[36];
extern char pepeaddress5[36];
extern char pepeaddress6[36];
extern char pepeaddress7[36];
extern char pepeaddress8[36];

extern char bellsaddress1[36];
extern char bellsaddress2[36];
extern char bellsaddress3[36];
extern char bellsaddress4[36];
extern char bellsaddress5[36];
extern char bellsaddress6[36];
extern char bellsaddress7[36];
extern char bellsaddress8[36];

extern char mnemonic1[109];
extern char mnemonic2[109];
extern char mnemonic3[109];
extern char mnemonic4[109];
extern char mnemonic5[109];
extern char mnemonic6[109];
extern char mnemonic7[109];
extern char mnemonic8[109];

#endif