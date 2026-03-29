#pragma bank 4

#include "bip39_wordlist.h"
#include <gbdk/platform.h>
#include <string.h>
#include <ctype.h>  // for tolower()
#include "sha256.h"
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

int bip39_get_word_index(const char* word) NONBANKED {
    if (!word || !word[0]) return -1;

    char lower[9];
    int i = 0;
    while (word[i] && i < 8) {
        lower[i] = tolower(word[i]);
        i++;
    }
    lower[i] = '\0';

    int low = 0;
    int high = 2047;
    while (low <= high) {
        int mid = (low + high) >> 1;
        char buf[9];
        get_bip39_word(mid, buf);
        int cmp = strcmp(lower, buf);
        if (cmp == 0) return mid;
        if (cmp < 0) high = mid - 1;
        else low = mid + 1;
    }
    return -1;
}

uint8_t bip39_checksum_valid(const char* mnemonic) BANKED {
    uint8_t entropy[16] = {0};
    uint8_t bitpos = 0;
    const char* p = mnemonic;
    int wordcnt = 0;
    int last_idx = -1;

    while (*p && wordcnt < 12) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;

        char word[9];
        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 8) {
            word[i++] = *p++;
        }
        word[i] = '\0';

        int idx = bip39_get_word_index(word);
        if (idx < 0) return 0;

        last_idx = idx;
        wordcnt++;

        
        int bits = (wordcnt < 12) ? 11 : 7;
        for (int b = 10; b > 10 - bits; b--) {
            if (idx & (1 << b)) {
                entropy[bitpos >> 3] |= 0x80 >> (bitpos & 7);
            }
            bitpos++;
        }
    }

    if (wordcnt != 12) return 0;

    
    uint8_t expected = last_idx & 0x0F;

    SHA256_CTX ctx;
    uint8_t hash[32];
    sha256_init(&ctx);
    sha256_update(&ctx, entropy, 16);
    sha256_final(&ctx, hash);

    return ((hash[0] >> 4) == expected) ? 1 : 0;
}