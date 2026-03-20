#ifndef HD_WALLET_H
#define HD_WALLET_H

#include <stdint.h>
#include <gb/gb.h>

void seed_to_addresses(
    const uint8_t *seed,
    char *doge_out,
    char *pepe_out,
    char *bells_out,
    uint8_t privkey_out[32],
    uint8_t pubkey_out[33]
) BANKED;

#endif