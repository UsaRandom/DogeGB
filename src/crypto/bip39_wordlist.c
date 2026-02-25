#pragma bank 4

#include "bip39_wordlist.h"
#include <gbdk/platform.h>
#include <string.h>
#include <ctype.h>  // for tolower()

#include <gb/gb.h>

extern const char* const bip39_words_1[1024];
extern const char* const bip39_words_2[1024];

BANKREF_EXTERN(bip39_words_1)
BANKREF_EXTERN(bip39_words_2)

char* get_bip39_word(int index, char* buffer) NONBANKED {
    uint8_t old_bank = CURRENT_BANK;
    const char* word_ptr = 0;
    
    if (index < 1024) {
        SWITCH_ROM(BANK(bip39_words_1));
        word_ptr = bip39_words_1[index];
    } else {
        SWITCH_ROM(BANK(bip39_words_2));
        word_ptr = bip39_words_2[index - 1024];
    }
    
    strcpy(buffer, word_ptr);
    
    SWITCH_ROM(old_bank);
    return buffer;
}

char* find_unique_word(const char* prefix) BANKED {
    static char unique[9];
    unique[0] = '\0';

    int pre_len = strlen(prefix);
    if (pre_len < 1) return unique;

    char lower_prefix[9];
    for (int i = 0; i < pre_len; i++) {
        lower_prefix[i] = tolower(prefix[i]);
    }
    lower_prefix[pre_len] = '\0';

    int low = 0;
    int high = 2047;
    while (low < high) {
        int mid = (low + high) / 2;
        char buf[9];
        get_bip39_word(mid, buf);
        if (strncmp(lower_prefix, buf, pre_len) > 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    int start = low;

    char first_buf[9];
    get_bip39_word(start, first_buf);
    if (strncmp(lower_prefix, first_buf, pre_len) != 0) {
        return unique;  
    }

    low = start;
    high = 2048;
    while (low < high) {
        int mid = (low + high) / 2;
        char buf[9];
        get_bip39_word(mid, buf);
        if (strncmp(lower_prefix, buf, pre_len) == 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    int end = low;

    int match_count = end - start;

    for (int i = start; i < end; i++) {
        char buf[9];
        get_bip39_word(i, buf);
        if (strcmp(lower_prefix, buf) == 0) {
            strcpy(unique, buf);
            return unique;
        }
    }
    
    if (match_count == 1) {
        char buf[9];
        get_bip39_word(start, buf);
        strcpy(unique, buf);
        return unique;
    }
    
    return unique;
}