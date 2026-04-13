#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "compat.h"

#include "../src/crypto/mnemonic.h"
#include "../src/crypto/hd_wallet.h"
#include "../src/crypto/tx_signer.h"

// Parse hex string into bytes. Returns number of bytes, or -1 on error.
static int hex_to_bytes(const char *hex, uint8_t *out, int out_max) {
    int len = (int)strlen(hex);
    if (len % 2 != 0) return -1;
    int n = len / 2;
    if (n > out_max) return -1;
    for (int i = 0; i < n; i++) {
        unsigned int b;
        if (sscanf(hex + 2*i, "%02x", &b) != 1) return -1;
        out[i] = (uint8_t)b;
    }
    return n;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s \"mnemonic phrase\" <unsigned_tx_hex>\n", argv[0]);
        return 1;
    }

    const char *mnemonic = argv[1];
    const char *tx_hex   = argv[2];

    // Derive key from mnemonic
    uint8_t seed[64];
    uint8_t privkey[32];
    uint8_t pubkey[33];
    char doge[36], pepe[36], bells[36];

    mnemonic_to_seed(mnemonic, seed);
    seed_to_addresses(seed, doge, pepe, bells, privkey, pubkey);

    // Decode unsigned tx
    static uint8_t tx_buf[4096];
    int tx_len = hex_to_bytes(tx_hex, tx_buf, (int)sizeof(tx_buf));
    if (tx_len < 0) {
        fprintf(stderr, "Invalid tx hex\n");
        return 1;
    }

    // Sign
    static uint8_t signed_buf[8192];
    uint16_t signed_len = 0;
    uint8_t ok = sign_tx(tx_buf, (uint16_t)tx_len,
                         privkey, pubkey,
                         signed_buf, (uint16_t)sizeof(signed_buf),
                         &signed_len);
    if (!ok) {
        fprintf(stderr, "sign_tx failed\n");
        return 1;
    }

    // Output signed tx as hex
    for (uint16_t i = 0; i < signed_len; i++)
        printf("%02x", signed_buf[i]);
    printf("\n");

    return 0;
}
