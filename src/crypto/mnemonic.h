#ifndef MNEMONIC_H
#define MNEMONIC_H

#include <gb/gb.h>
#include <stdint.h>

// Function declarations
void generate_mnemonic(char words[12][9]) BANKED;
void mnemonic_to_seed(const char *mnemonic, uint8_t *seed) BANKED;

#endif