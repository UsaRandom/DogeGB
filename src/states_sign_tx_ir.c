#pragma bank 9

#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <string.h>
#include "states.h"
#include "ir_transport.h"
#include "crypto/tx_parser.h"
#include "crypto/tx_signer.h"
#include "wallet.h"

extern AppState current_state;
extern uint8_t  current_mode;
extern wallet   current_wallet;

/* Receive buffer — 256 bytes covers typical 1-4 input DOGE TXs (~160-250 bytes) */
static uint8_t rx_buf[256];

/* Parsed TX lives in WRAM */
static ParsedTx parsed;

/* Output buffer for the assembled signed TX.
   Worst-case signed size = unsigned(≤256) + 4×109 bytes per input = ~692 bytes.
   480 bytes handles up to ~2 inputs; larger TXs will be rejected at sign time. */
static uint8_t signed_buf[480];

/* -----------------------------------------------------------------------
 * Coin ticker string
 * ----------------------------------------------------------------------- */
static const char *coin_ticker(void) {
    if (current_mode == 1) return "BELLS";
    if (current_mode == 2) return "PEPE";
    return "DOGE";
}

/* -----------------------------------------------------------------------
 * Display one output.
 * ----------------------------------------------------------------------- */
static void display_output(uint8_t idx, TxOutputDisplay *o) {
    uint8_t alen = (uint8_t)strlen(o->addr);

    printf("OUT%u: %s %s\n", (unsigned)(idx + 1u), o->value, coin_ticker());

    if (alen <= 20u) {
        printf("%s\n", o->addr);
    } else {
        uint8_t i;
        for (i = 0; i < 19u; i++) putchar(o->addr[i]);
        putchar('>');
        putchar('\n');
        printf("%s\n", o->addr + 19u);
    }
}

/* -----------------------------------------------------------------------
 * Confirmation screen — returns 1 if user pressed A (sign), 0 for B (no)
 * ----------------------------------------------------------------------- */
static uint8_t show_confirm_screen(void) {
    uint8_t i;

    cls();
    gotoxy(0, 0);
    printf("-- TX CONFIRM --\n");
    printf("A=Sign  B=Reject\n");
    printf("----------------\n");

    for (i = 0; i < parsed.n_outputs; i++) {
        display_output(i, &parsed.outputs[i]);
    }

    printf("----------------\n");
    printf("Inputs: %u UTXO%s\n",
           (unsigned)parsed.n_inputs,
           parsed.n_inputs == 1u ? "" : "s");

    for (;;) {
        uint8_t keys = joypad();
        if (keys & J_A) { while (joypad()) vsync(); return 1; }
        if (keys & J_B) { while (joypad()) vsync(); return 0; }
        vsync();
    }
}

/* -----------------------------------------------------------------------
 * Main state handler
 * ----------------------------------------------------------------------- */
void handle_sign_tx_ir(void) BANKED {
    uint16_t payload_len = 0;
    uint8_t  msg_type    = 0;

    cls();
    gotoxy(0, 0);
    printf("IR Receive\n");
    printf("Any key: cancel\n");
    printf("------------------\n");
    printf("Waiting for TX...\n");

    /* ---- receive loop ---- */
    for (;;) {
        if (joypad()) {
            current_state = STATE_WALLET_MENU;
            return;
        }

        msg_type = ir_recv_message(rx_buf, sizeof(rx_buf), &payload_len);

        if (msg_type == 0 || msg_type == 0xFF) continue;
        if (msg_type != IR_MSG_TX_PROPOSAL) continue;
        break;
    }

    /* ---- parse ---- */
    cls();
    gotoxy(0, 0);

    if (!parse_tx(rx_buf, payload_len, current_mode, &parsed)) {
        printf("Parse error!\n\n");
        printf("Any key: back");
        while (!joypad()) vsync();
        while (joypad()) vsync();
        current_state = STATE_WALLET_MENU;
        return;
    }

    /* ---- confirm ---- */
    if (!show_confirm_screen()) {
        current_state = STATE_WALLET_MENU;
        return;
    }

    /* ---- sign ---- */
    cls();
    gotoxy(0, 0);
    printf("Signing...\n");

    if (current_wallet.slotNum == 255u) {
        printf("No wallet!\n\n");
        printf("Any key: back");
        while (!joypad()) vsync();
        while (joypad()) vsync();
        current_state = STATE_WALLET_MENU;
        return;
    }

    uint16_t signed_len = 0;
    uint8_t ok = sign_tx(rx_buf, payload_len,
                         current_wallet.private_key, current_wallet.public_key,
                         signed_buf, (uint16_t)sizeof(signed_buf), &signed_len);

    if (!ok) {
        printf("Sign failed!\n\n");
        printf("Any key: back");
        while (!joypad()) vsync();
        while (joypad()) vsync();
        current_state = STATE_WALLET_MENU;
        return;
    }

    /* ---- send signed TX ---- */
    cls();
    gotoxy(0, 0);
    printf("IR Send\n");
    printf("Any key: cancel\n");
    printf("------------------\n");
    printf("Sending signed TX\n");

    for (;;) {
        if (joypad()) {
            current_state = STATE_WALLET_MENU;
            return;
        }

        if (ir_send_message(IR_MSG_SIGNED_TX, signed_buf, signed_len) == IR_OK)
            break;
    }

    cls();
    gotoxy(0, 0);
    printf("TX Sent!\n\n");
    printf("Any key: back");
    while (!joypad()) vsync();
    while (joypad()) vsync();
    current_state = STATE_WALLET_MENU;
}
