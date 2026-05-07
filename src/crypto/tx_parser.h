#ifndef TX_PARSER_H
#define TX_PARSER_H

#include <stdint.h>
#include <gb/gb.h>

#define TX_MAX_OUTPUTS  4

// Script type constants
#define SCRIPT_P2PKH    0
#define SCRIPT_P2SH     1
#define SCRIPT_OPRETURN 2
#define SCRIPT_UNKNOWN  3

// Per-output display info
typedef struct {
    char addr[36];      // base58check or "OP_RETURN"
    char value[14];     // "1234.567\0"
    uint8_t script_type;
} TxOutputDisplay;

// inputs[] removed — only the count is needed for display
typedef struct {
    uint8_t         n_inputs;
    uint8_t         n_outputs;
    TxOutputDisplay outputs[TX_MAX_OUTPUTS];
    uint8_t         valid;
} ParsedTx;

// coin_mode: 0=DOGE, 1=BELLS, 2=PEPE  (matches WALLET_MODE)
uint8_t parse_tx(const uint8_t *tx, uint16_t tx_len,
                 uint8_t coin_mode, ParsedTx *out) BANKED;

#endif
