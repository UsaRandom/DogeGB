#pragma bank 6

#include "tx_parser.h"
#include "sha256.h"
#include "ripemd160.h"

#include <string.h>
#include <stdint.h>

// P2PKH version bytes: DOGE=0x1E, BELLS=0x19, PEPE=0x38
static const uint8_t P2PKH_VER[3] = { 0x1E, 0x19, 0x38 };
// P2SH version bytes:  DOGE=0x16, BELLS=0x05, PEPE=0x05
static const uint8_t P2SH_VER[3]  = { 0x16, 0x05, 0x05 };

static const char HEX_CHARS[] = "0123456789abcdef";

static const char BASE58_ALPHA[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// ---- varint ----

static uint32_t read_varint(const uint8_t *buf, uint16_t buf_len,
                            uint16_t *pos, uint8_t *ok) {
    if (*pos >= buf_len) { *ok = 0; return 0; }
    uint8_t first = buf[(*pos)++];
    if (first < 0xFD) return first;
    if (first == 0xFD) {
        if (*pos + 2 > buf_len) { *ok = 0; return 0; }
        uint32_t v = buf[*pos] | ((uint32_t)buf[*pos+1] << 8);
        *pos += 2; return v;
    }
    // 0xFE/0xFF – too large for our use
    *ok = 0; return 0;
}

// ---- hex helpers ----

static void byte_to_hex(uint8_t b, char *out) {
    out[0] = HEX_CHARS[b >> 4];
    out[1] = HEX_CHARS[b & 0x0F];
}

// Format txid: display as big-endian (reversed), first 4 + "..." + last 4 hex chars
// txid_le: 32 bytes little-endian (as stored in tx), out: "ab12cd34...ef56ab78\0" (18+1)
// We do a shorter version: 4 hex chars + "..." + 4 hex chars = 11 chars + null
static void format_txid(const uint8_t *txid_le, char *out) {
    // txid display = big-endian = reverse of txid_le
    // Show first 2 bytes BE (= last 2 bytes of LE) and last 2 bytes BE (= first 2 bytes of LE)
    byte_to_hex(txid_le[31], out + 0);
    byte_to_hex(txid_le[30], out + 2);
    out[4] = '.'; out[5] = '.'; out[6] = '.';
    byte_to_hex(txid_le[1],  out + 7);
    byte_to_hex(txid_le[0],  out + 9);
    out[11] = '\0';
}

// ---- base58check ----

static void sha256_hash(const uint8_t *data, uint16_t len, uint8_t *out) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

static void base58check_encode(const uint8_t *payload, uint8_t plen, char *out) {
    uint8_t h1[32], h2[32];
    uint8_t buf[25]; // max: 1 ver + 20 hash + 4 checksum

    sha256_hash(payload, plen, h1);
    sha256_hash(h1, 32, h2);

    memcpy(buf, payload, plen);
    buf[plen]   = h2[0]; buf[plen+1] = h2[1];
    buf[plen+2] = h2[2]; buf[plen+3] = h2[3];

    uint8_t total = plen + 4;
    int zeros = 0;
    while (zeros < total && buf[zeros] == 0) zeros++;

    static uint8_t b58[40];
    int b58_len = 0;
    memset(b58, 0, 40);

    for (int i = 0; i < total; i++) {
        int carry = buf[i];
        for (int j = 0; j < b58_len; j++) {
            carry += (int)b58[j] << 8;
            b58[j] = carry % 58;
            carry /= 58;
        }
        while (carry > 0) {
            b58[b58_len++] = carry % 58;
            carry /= 58;
        }
    }

    char *p = out;
    for (int i = 0; i < zeros; i++) *p++ = '1';
    for (int i = b58_len - 1; i >= 0; i--) *p++ = BASE58_ALPHA[b58[i]];
    *p = '\0';
}

// Decode hash160 + version byte → base58check address
static void hash160_to_addr(const uint8_t *hash160, uint8_t version, char *out) {
    uint8_t payload[21];
    payload[0] = version;
    memcpy(payload + 1, hash160, 20);
    base58check_encode(payload, 21, out);
}

// ---- value formatting ----
// val_le8: 8-byte little-endian satoshi value
// Dogecoin/Pepecoin/Bellscoin use 8 decimal places
static void format_value(const uint8_t *val_le8, char *out) {
    // Reconstruct as two 32-bit halves (avoid full 64-bit division on SDCC)
    uint32_t lo = (uint32_t)val_le8[0]        | ((uint32_t)val_le8[1] << 8)
                | ((uint32_t)val_le8[2] << 16) | ((uint32_t)val_le8[3] << 24);
    uint32_t hi = (uint32_t)val_le8[4]        | ((uint32_t)val_le8[5] << 8)
                | ((uint32_t)val_le8[6] << 16) | ((uint32_t)val_le8[7] << 24);

    // Convert to coins (divide by 100000000 = 1e8)
    // Use 64-bit division — SDCC supports uint64_t
    uint64_t satoshis = ((uint64_t)hi << 32) | lo;
    uint64_t whole    = satoshis / 100000000ULL;
    uint32_t frac     = (uint32_t)(satoshis % 100000000ULL);

    // Print whole part (simple itoa)
    char tmp[20];
    int tlen = 0;
    if (whole == 0) {
        tmp[tlen++] = '0';
    } else {
        uint64_t w = whole;
        while (w > 0) { tmp[tlen++] = '0' + (w % 10); w /= 10; }
        // reverse
        for (int i = 0; i < tlen / 2; i++) {
            char c = tmp[i]; tmp[i] = tmp[tlen-1-i]; tmp[tlen-1-i] = c;
        }
    }
    memcpy(out, tmp, tlen);
    out[tlen++] = '.';

    // Print frac part, zero-padded to 8 digits, strip trailing zeros
    char fstr[9];
    for (int i = 7; i >= 0; i--) {
        fstr[i] = '0' + (frac % 10);
        frac /= 10;
    }
    fstr[8] = '\0';
    // strip trailing zeros (keep at least 2)
    int flen = 8;
    while (flen > 2 && fstr[flen-1] == '0') flen--;
    memcpy(out + tlen, fstr, flen);
    out[tlen + flen] = '\0';
}

// ---- script decoding ----

static uint8_t decode_script(const uint8_t *script, uint16_t slen,
                              uint8_t coin_mode, TxOutputDisplay *od) {
    if (slen == 25 &&
        script[0] == 0x76 && script[1] == 0xa9 && script[2] == 0x14 &&
        script[23] == 0x88 && script[24] == 0xac) {
        // P2PKH
        od->script_type = SCRIPT_P2PKH;
        hash160_to_addr(script + 3, P2PKH_VER[coin_mode], od->addr);
        return 1;
    }
    if (slen == 23 &&
        script[0] == 0xa9 && script[1] == 0x14 && script[22] == 0x87) {
        // P2SH
        od->script_type = SCRIPT_P2SH;
        hash160_to_addr(script + 2, P2SH_VER[coin_mode], od->addr);
        return 1;
    }
    if (slen >= 1 && script[0] == 0x6a) {
        // OP_RETURN
        od->script_type = SCRIPT_OPRETURN;
        memcpy(od->addr, "OP_RETURN", 10);
        return 1;
    }
    od->script_type = SCRIPT_UNKNOWN;
    return 0;
}

// ---- main parser ----

uint8_t parse_tx(const uint8_t *tx, uint16_t tx_len,
                 uint8_t coin_mode, ParsedTx *out) BANKED {
    uint16_t pos = 0;
    uint8_t ok = 1;

    memset(out, 0, sizeof(ParsedTx));

    // version (4 bytes LE)
    if (pos + 4 > tx_len) return 0;
    pos += 4;

    // input count
    uint32_t n_in = read_varint(tx, tx_len, &pos, &ok);
    if (!ok || n_in == 0 || n_in > TX_MAX_INPUTS) return 0;
    out->n_inputs = (uint8_t)n_in;

    for (uint8_t i = 0; i < n_in; i++) {
        // txid (32 bytes)
        if (pos + 32 > tx_len) return 0;
        format_txid(tx + pos, out->inputs[i].txid);
        pos += 32;
        // vout (4 bytes LE)
        if (pos + 4 > tx_len) return 0;
        out->inputs[i].vout = (uint32_t)tx[pos]          | ((uint32_t)tx[pos+1] << 8)
                            | ((uint32_t)tx[pos+2] << 16) | ((uint32_t)tx[pos+3] << 24);
        pos += 4;
        // scriptSig (varint + data) — skip
        uint32_t sslen = read_varint(tx, tx_len, &pos, &ok);
        if (!ok || pos + sslen > tx_len) return 0;
        pos += (uint16_t)sslen;
        // sequence (4 bytes)
        if (pos + 4 > tx_len) return 0;
        pos += 4;
    }

    // output count
    uint32_t n_out = read_varint(tx, tx_len, &pos, &ok);
    if (!ok || n_out == 0 || n_out > TX_MAX_OUTPUTS) return 0;
    out->n_outputs = (uint8_t)n_out;

    for (uint8_t i = 0; i < n_out; i++) {
        // value (8 bytes LE)
        if (pos + 8 > tx_len) return 0;
        format_value(tx + pos, out->outputs[i].value);
        pos += 8;
        // scriptPubKey
        uint32_t spklen = read_varint(tx, tx_len, &pos, &ok);
        if (!ok || pos + spklen > tx_len) return 0;
        if (!decode_script(tx + pos, (uint16_t)spklen, coin_mode, &out->outputs[i]))
            return 0; // unsupported script type
        pos += (uint16_t)spklen;
    }

    // locktime (4 bytes)
    if (pos + 4 > tx_len) return 0;
    pos += 4;

    if (pos != tx_len) return 0; // trailing garbage

    out->valid = 1;
    return 1;
}
