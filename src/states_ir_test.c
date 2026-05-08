#pragma bank 9

#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>
#include "states.h"
#include "ir_transport.h"

extern AppState current_state;

void handle_ir_test(void) BANKED {
    uint8_t  counter   = 0;
    uint16_t ok_count  = 0;
    uint16_t err_count = 0;

    uint8_t  ping_buf[32];
    uint8_t  pong_buf[32];
    uint16_t plen;
    uint8_t  msg_type;
    uint8_t  tries;
    uint8_t  got_pong;
    uint8_t  i;

    cls();
    gotoxy(0, 0);
    printf("-- IR Test --\n");
    printf("Any key: exit\n");
    printf("--------------\n");

    for (;;) {
        if (joypad()) {
            while (joypad()) vsync();
            current_state = STATE_SLOT_SELECTION;
            return;
        }

        /* Update display */
        gotoxy(0, 3);
        printf("Ping #%03u\n", (unsigned)counter);
        printf("OK:  %-5u\n", (unsigned)ok_count);
        printf("ERR: %-5u\n", (unsigned)err_count);
        printf("Sending...    \n");

        /* Build 32-byte ping payload: fill with 0xA5, counter in last byte */
        for (i = 0; i < 31u; i++) ping_buf[i] = 0xA5u;
        ping_buf[31] = counter;

        if (ir_send_message(IR_MSG_PING, ping_buf, 32u) != IR_OK) {
            err_count++;
            gotoxy(9, 6);
            printf("TX FAIL\n");
            continue;
        }

        /* Wait for PONG — poll ir_recv_message in a tight loop.
           Each call times out in ~36ms; 30 tries = ~1.1s total window. */
        gotoxy(9, 6);
        printf("Waiting\n");
        got_pong = 0;
        for (tries = 0; tries < 30u; tries++) {
            if (joypad()) {
                while (joypad()) vsync();
                current_state = STATE_SLOT_SELECTION;
                return;
            }
            msg_type = ir_recv_message(pong_buf, 32u, &plen);
            if (msg_type == IR_MSG_PONG && plen >= 32u && pong_buf[31] == counter) {
                got_pong = 1;
                break;
            }
            if (msg_type != 0) {
                /* Got something, but not our pong — keep waiting */
            }
        }

        if (got_pong) {
            ok_count++;
            counter++;
            gotoxy(9, 6);
            printf("OK!    \n");
        } else {
            err_count++;
            gotoxy(9, 6);
            printf("TIMEOUT\n");
        }
    }
}
