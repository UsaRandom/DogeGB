#ifndef IR_TRANSPORT_H
#define IR_TRANSPORT_H

#include <stdint.h>
#include <gb/gb.h>

/* Chunk magic */
#define IR_MAGIC_0      0xDC
#define IR_MAGIC_1      0x6E

/* Chunk types */
#define IR_CHUNK_DATA   0x01
#define IR_CHUNK_ACK    0x02
#define IR_CHUNK_NACK   0x03
#define IR_CHUNK_PING   0x04
#define IR_CHUNK_PONG   0x05

/* Application message types */
#define IR_MSG_TX_PROPOSAL  0x01
#define IR_MSG_SIGNED_TX    0x02
#define IR_MSG_PING         0x03
#define IR_MSG_PONG         0x04
#define IR_MSG_ERROR        0x05

/* Return codes */
#define IR_OK           0
#define IR_TIMEOUT      1
#define IR_CRC_ERROR    2
#define IR_BUF_FULL     3
#define IR_PROTO_ERROR  4

/* Max data bytes per chunk (must match companion) */
#define IR_MAX_CHUNK_DATA 64u

/*
 * Receive one complete application message.
 * Sends a per-chunk ACK/NACK for each DATA chunk.
 * Returns the message type (IR_MSG_*) on success.
 * Returns 0 on timeout (no signal), 0xFF on protocol/CRC error.
 * On success, buf[0..(*payload_len)-1] holds the payload.
 */
uint8_t ir_recv_message(uint8_t *buf, uint16_t buf_max,
                        uint16_t *payload_len) BANKED;

/*
 * Send one complete application message in chunks.
 * Waits for a per-chunk ACK from the peer.
 * Returns IR_OK on success, IR_TIMEOUT if ACK never arrived.
 */
uint8_t ir_send_message(uint8_t msg_type, const uint8_t *payload,
                        uint16_t payload_len) BANKED;

#endif
