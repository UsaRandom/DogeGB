#ifndef TX_PARSER_H
#define TX_PARSER_H

#include <stdint.h>
#include <gb/gb.h>

#define TX_MAX_INPUTS   8
#define TX_MAX_OUTPUTS  8

// Script type constants
#define SCRIPT_P2PKH    0
#define SCRIPT_P2SH     1
#define SCRIPT_OPRETURN 2
#define SCRIPT_UNKNOWN  3

// Per-input display info
typedef struct {
    char txid[12];      // "ab12...cd34\0"
    uint32_t vout;
} TxInputDisplay;

// Per-output display info
typedef struct {
    char addr[36];      // base58check or "OP_RETURN"
    char value[14];     // "123456.78\0"
    uint8_t script_type;
} TxOutputDisplay;

typedef struct {
    uint8_t        n_inputs;
    uint8_t        n_outputs;
    TxInputDisplay inputs[TX_MAX_INPUTS];
    TxOutputDisplay outputs[TX_MAX_OUTPUTS];
    uint8_t        valid; // 0 on parse error or unsupported script
} ParsedTx;

// coin_mode: 0=DOGE, 1=BELLS, 2=PEPE  (matches WALLET_MODE)
uint8_t parse_tx(const uint8_t *tx, uint16_t tx_len,
                 uint8_t coin_mode, ParsedTx *out) BANKED;

#endif
