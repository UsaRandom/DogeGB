#ifndef BIP39_WORDLIST_H
#define BIP39_WORDLIST_H

#include <gb/gb.h>

char* get_bip39_word(int index, char* buffer) NONBANKED;
char* find_unique_word(const char* prefix) BANKED;
int bip39_get_word_index(const char* word) NONBANKED;
uint8_t bip39_checksum_valid(const char* mnemonic) BANKED;

#endif
