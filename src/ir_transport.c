#pragma bank 8

#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ir_transport.h"

/* -----------------------------------------------------------------------
 * IR hardware
 * ----------------------------------------------------------------------- */
#define IR_ON()    (RP_REG = 0xC1)
#define IR_OFF()   (RP_REG = 0xC0)
#define IR_LIGHT() (!(RP_REG & 0x02))

/* -----------------------------------------------------------------------
 * Delays  (CGB double-speed, ~3.34 us per loop iteration)
 * ----------------------------------------------------------------------- */
static void delay_500us(void) __naked {
    __asm
        ld bc, #150
    1$: dec bc
        ld a, b
        or c
        jr nz, 1$
        ret
    __endasm;
}

static void delay_1ms(void) __naked {
    __asm
        ld bc, #299
    1$: dec bc
        ld a, b
        or c
        jr nz, 1$
        ret
    __endasm;
}

static void delay_3ms(void) __naked {
    __asm
        ld bc, #898
    1$: dec bc
        ld a, b
        or c
        jr nz, 1$
        ret
    __endasm;
}

static void delay_80ms(void) __naked {
    __asm
        ld bc, #23952
    1$: dec bc
        ld a, b
        or c
        jr nz, 1$
        ret
    __endasm;
}

/* -----------------------------------------------------------------------
 * Bit-level TX
 * ----------------------------------------------------------------------- */
static void ir_tx_bit(uint8_t bit) {
    IR_ON();  delay_500us();
    IR_OFF();
    if (bit) delay_3ms();
    else     delay_1ms();
}

static void ir_tx_byte(uint8_t val) {
    ir_tx_bit(1); /* start bit */
    for (uint8_t i = 0; i < 8; i++) {
        ir_tx_bit((val & 0x80) ? 1 : 0);
        val <<= 1;
    }
    IR_ON(); delay_500us(); IR_OFF(); /* terminator pulse */
    delay_3ms(); delay_3ms();        /* ~6 ms inter-byte gap */
}

/* -----------------------------------------------------------------------
 * Bit-level RX
 * DIV increments at 32768 Hz in CGB double-speed => ~30.5 us per tick.
 * bit-0 gap ~33 ticks, bit-1 gap ~98 ticks; threshold 66 ticks.
 * ----------------------------------------------------------------------- */
#define GAP_THRESHOLD 66u

static uint8_t ir_rx_byte(uint8_t *out) {
    uint8_t t0, gap;
    uint16_t safety;
    uint8_t val = 0;

    for (uint8_t i = 0; i < 9; i++) {
        /* wait for falling edge (end of pulse) */
        safety = 5000;
        while (IR_LIGHT()) { if (--safety == 0) return 0; }

        t0 = DIV_REG;
        safety = 10000;
        while (!IR_LIGHT()) { if (--safety == 0) return 0; }
        gap = DIV_REG - t0; /* 8-bit subtraction wraps correctly */

        if (i == 0) {
            if (gap < GAP_THRESHOLD) return 0; /* bad start bit */
        } else {
            val = (val << 1) | ((gap > GAP_THRESHOLD) ? 1 : 0);
        }
    }

    /* consume terminator pulse */
    safety = 5000;
    while (IR_LIGHT()) { if (--safety == 0) return 0; }

    *out = val;
    return 1;
}

/* -----------------------------------------------------------------------
 * Multi-byte send / receive (interrupts disabled during transfer)
 * ----------------------------------------------------------------------- */
static void ir_send_n(const uint8_t *buf, uint16_t n) {
    disable_interrupts();
    for (uint16_t i = 0; i < n; i++) ir_tx_byte(buf[i]);
    enable_interrupts();
}

/* Returns bytes received; 0 means first-byte timeout. */
static uint16_t ir_recv_n(uint8_t *buf, uint16_t maxlen,
                           uint16_t first_timeout) {
    uint16_t i = 0;
    uint8_t  b;
    uint16_t timeout;

    disable_interrupts();
    IR_OFF(); /* sets RP_REG=0xC0, enabling the read circuit (bit 6) */

    timeout = first_timeout;
    while (!IR_LIGHT()) {
        if (--timeout == 0) { enable_interrupts(); return 0; }
    }
    if (!ir_rx_byte(&b)) { enable_interrupts(); return 0; }
    if (i < maxlen) buf[i++] = b;

    while (i < maxlen) {
        timeout = 10000U;
        while (!IR_LIGHT()) {
            if (--timeout == 0) goto done;
        }
        if (!ir_rx_byte(&b)) goto done;
        buf[i++] = b;
    }
done:
    enable_interrupts();
    return i;
}

/* -----------------------------------------------------------------------
 * CRC32 — IEEE 802.3, reflected polynomial 0xEDB88320
 * ----------------------------------------------------------------------- */
static uint32_t crc32(const uint8_t *data, uint16_t n) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1UL) crc = (crc >> 1) ^ 0xEDB88320UL;
            else            crc = (crc >> 1);
        }
    }
    return ~crc;
}

/* -----------------------------------------------------------------------
 * Chunk-level framing
 *
 * Wire format per chunk (10..202 bytes):
 *   [0xDC][0x6E][TYPE][SEQ][TOTAL][LEN][DATA x LEN][CRC32 BE x 4]
 * ----------------------------------------------------------------------- */

/* Static receive buffer — one chunk at a time, avoids stack pressure. */
static uint8_t s_chunk_buf[6u + IR_MAX_CHUNK_DATA + 4u];

/*
 * Receive one raw chunk.
 * Returns chunk type (IR_CHUNK_*) on success.
 * Returns 0xFF on first-byte timeout, 0xFE on bad frame / bad CRC.
 * Fills seq, total, data_out[0..*data_len-1] on success.
 */
static uint8_t recv_one_chunk(uint8_t *seq_out, uint8_t *total_out,
                               uint8_t *data_out, uint8_t *data_len_out,
                               uint16_t first_timeout) {
    uint16_t n = ir_recv_n(s_chunk_buf, sizeof(s_chunk_buf), first_timeout);
    if (n == 0) return 0xFF; /* timeout */
    if (n < 10u) return 0xFE;
    if (s_chunk_buf[0] != IR_MAGIC_0 || s_chunk_buf[1] != IR_MAGIC_1) return 0xFE;

    uint8_t type  = s_chunk_buf[2];
    uint8_t seq   = s_chunk_buf[3];
    uint8_t total = s_chunk_buf[4];
    uint8_t len   = s_chunk_buf[5];

    if ((uint16_t)(6u + len + 4u) != n) return 0xFE;

    uint32_t want = crc32(s_chunk_buf, (uint16_t)(6u + len));
    uint32_t got  = ((uint32_t)s_chunk_buf[n - 4u] << 24)
                  | ((uint32_t)s_chunk_buf[n - 3u] << 16)
                  | ((uint32_t)s_chunk_buf[n - 2u] <<  8)
                  |  (uint32_t)s_chunk_buf[n - 1u];
    if (want != got) return 0xFE;

    *seq_out      = seq;
    *total_out    = total;
    *data_len_out = len;
    for (uint8_t i = 0; i < len; i++) data_out[i] = s_chunk_buf[6u + i];
    return type;
}

static void send_ack(uint8_t seq) {
    uint8_t frame[10];
    frame[0] = IR_MAGIC_0; frame[1] = IR_MAGIC_1;
    frame[2] = IR_CHUNK_ACK; frame[3] = seq;
    frame[4] = 0; frame[5] = 0;
    uint32_t c = crc32(frame, 6);
    frame[6] = (uint8_t)(c >> 24); frame[7] = (uint8_t)(c >> 16);
    frame[8] = (uint8_t)(c >>  8); frame[9] = (uint8_t)c;
    ir_send_n(frame, 10);
}

static void send_nack(void) {
    uint8_t frame[10];
    frame[0] = IR_MAGIC_0; frame[1] = IR_MAGIC_1;
    frame[2] = IR_CHUNK_NACK; frame[3] = 0;
    frame[4] = 0; frame[5] = 0;
    uint32_t c = crc32(frame, 6);
    frame[6] = (uint8_t)(c >> 24); frame[7] = (uint8_t)(c >> 16);
    frame[8] = (uint8_t)(c >>  8); frame[9] = (uint8_t)c;
    ir_send_n(frame, 10);
}

/* Static send frame — avoids stack pressure during chunked send. */
static uint8_t s_send_frame[6u + IR_MAX_CHUNK_DATA + 4u];

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/*
 * Receive one complete application message.
 *
 * The assembled bytes form:  [msg_type(1)][len_hi(1)][len_lo(1)][payload...]
 * On return, buf[0..*payload_len-1] = payload; return value = msg_type.
 * Returns 0 on first-chunk timeout (no signal), 0xFF on error.
 *
 * Called in a polling loop — first_timeout is short so the caller can
 * check joypad between calls.
 */
uint8_t ir_recv_message(uint8_t *buf, uint16_t buf_max,
                        uint16_t *payload_len) BANKED {
    uint8_t  expected_total = 0xFF;
    uint16_t assembled = 0;
    uint8_t  chunk_data[IR_MAX_CHUNK_DATA];

    /* Short first_timeout so the caller can poll joypad between calls. */
    uint16_t first_timeout = 20000U; /* ~few hundred ms at most */

    for (;;) {
        uint8_t seq, total, dlen;
        uint8_t type = recv_one_chunk(&seq, &total, chunk_data, &dlen,
                                      first_timeout);

        if (type == 0xFF) return 0;            /* timeout — caller may cancel */
        if (type == 0xFE) {
            send_nack();
            /* Mid-frame sync or CRC error — retry; the NACK prompts a resend. */
            first_timeout = 20000U;
            assembled = 0;
            expected_total = 0xFF;
            continue;
        }

        if (type != IR_CHUNK_DATA) {
            first_timeout = 20000U;
            continue;
        }

        /* Arduino waits ~40ms after its last TX byte before listening for ACK.
           Delay here so the Arduino is already in RX mode before we start. */
        delay_80ms();
        send_ack(seq);

        if (expected_total == 0xFF) expected_total = total;

        gotoxy(0, 4);
        printf("Chunk %u/%u   ", (unsigned)(seq + 1u), (unsigned)total);

        if ((uint16_t)(assembled + dlen) > buf_max) return 0xFF;
        for (uint8_t i = 0; i < dlen; i++) buf[assembled + i] = chunk_data[i];
        assembled += dlen;

        if (seq == (uint8_t)(total - 1u)) break; /* last chunk */

        /* Wait up to ~65ms for next chunk (covers USB serial round-trip). */
        first_timeout = 65000U;
    }

    if (assembled < 3u) return 0xFF;

    uint8_t  msg_type = buf[0];
    uint16_t plen     = ((uint16_t)buf[1] << 8) | buf[2];
    if ((uint16_t)(plen + 3u) > assembled) return 0xFF;

    /* Shift payload over the 3-byte envelope. */
    for (uint16_t i = 0; i < plen; i++) buf[i] = buf[i + 3u];
    *payload_len = plen;
    return msg_type;
}

/*
 * Send one complete application message.
 * Wraps payload in the 3-byte envelope, splits into chunks, sends each
 * and waits for ACK.  Returns IR_OK on full success, IR_TIMEOUT otherwise.
 */
uint8_t ir_send_message(uint8_t msg_type, const uint8_t *payload,
                        uint16_t payload_len) BANKED {
    uint16_t msg_len     = 3u + payload_len;
    uint8_t  total_chunks = (uint8_t)((msg_len + IR_MAX_CHUNK_DATA - 1u)
                                      / IR_MAX_CHUNK_DATA);
    uint16_t msg_offset  = 0;

    for (uint8_t seq = 0; seq < total_chunks; seq++) {
        /* Build chunk data from the virtual message stream. */
        uint8_t dlen = 0;
        while (dlen < IR_MAX_CHUNK_DATA && msg_offset < msg_len) {
            uint8_t b;
            if      (msg_offset == 0) b = msg_type;
            else if (msg_offset == 1) b = (uint8_t)(payload_len >> 8);
            else if (msg_offset == 2) b = (uint8_t)payload_len;
            else                      b = payload[msg_offset - 3u];
            s_send_frame[6u + dlen++] = b;
            msg_offset++;
        }

        s_send_frame[0] = IR_MAGIC_0;
        s_send_frame[1] = IR_MAGIC_1;
        s_send_frame[2] = IR_CHUNK_DATA;
        s_send_frame[3] = seq;
        s_send_frame[4] = total_chunks;
        s_send_frame[5] = dlen;

        uint32_t c = crc32(s_send_frame, (uint16_t)(6u + dlen));
        s_send_frame[6u + dlen + 0u] = (uint8_t)(c >> 24);
        s_send_frame[6u + dlen + 1u] = (uint8_t)(c >> 16);
        s_send_frame[6u + dlen + 2u] = (uint8_t)(c >>  8);
        s_send_frame[6u + dlen + 3u] = (uint8_t)c;

        ir_send_n(s_send_frame, (uint16_t)(6u + dlen + 4u));

        /* Wait for ACK. */
        uint8_t ack_seq, ack_total, ack_dlen;
        uint8_t ack_data[4];
        uint8_t ack_type = recv_one_chunk(&ack_seq, &ack_total,
                                          ack_data, &ack_dlen, 65000U);
        if (ack_type != IR_CHUNK_ACK || ack_seq != seq) return IR_TIMEOUT;

        /* After sending the ACK, the Arduino (handleTxR) has a 40ms dead zone
           before it starts listening for the next chunk, then a 200ms listen
           window.  Total: ~280ms before the companion's bridge.listen() is
           active.  Delay ~320ms so the next chunk arrives well after that
           window expires and the companion is in a fresh listen() call. */
        if ((uint8_t)(seq + 1u) < total_chunks) {
            delay_80ms(); delay_80ms(); delay_80ms(); delay_80ms();
        }
    }

    return IR_OK;
}
