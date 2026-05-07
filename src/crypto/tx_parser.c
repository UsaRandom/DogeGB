#pragma bank 8

#include "tx_parser.h"
#include "sha256.h"
#include "ripemd160.h"

#include <string.h>
#include <stdint.h>

/* Divide hi:lo by 10 in-place; return remainder digit (0-9).
   Works because rem < 10, so rem<<16 never overflows uint32_t. */
static uint8_t div64_10(uint32_t *hi, uint32_t *lo) {
    uint32_t r, mid, qm, low;
    r    = *hi % 10u;  *hi = *hi / 10u;
    mid  = (r << 16) | (*lo >> 16);
    qm   = mid / 10u;  r   = mid % 10u;
    low  = (r << 16) | (*lo & 0xFFFFu);
    *lo  = (qm << 16) | (low / 10u);
    return (uint8_t)(low % 10u);
}

// P2PKH version bytes: DOGE=0x1E, BELLS=0x19, PEPE=0x38
static const uint8_t P2PKH_VER[3] = { 0x1E, 0x19, 0x38 };
// P2SH version bytes:  DOGE=0x16, BELLS=0x05, PEPE=0x05
static const uint8_t P2SH_VER[3]  = { 0x16, 0x05, 0x05 };


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
// val_le8: 8-byte LE satoshi value → "X.XXX" (3 decimal places, no trailing zeros past 1st)
static void format_value(const uint8_t *val_le8, char *out) {
    uint32_t lo = (uint32_t)val_le8[0]        | ((uint32_t)val_le8[1] << 8)
                | ((uint32_t)val_le8[2] << 16) | ((uint32_t)val_le8[3] << 24);
    uint32_t hi = (uint32_t)val_le8[4]        | ((uint32_t)val_le8[5] << 8)
                | ((uint32_t)val_le8[6] << 16) | ((uint32_t)val_le8[7] << 24);

    /* Extract 18 decimal digits LSB-first; 8 are fractional, 10 are whole (max ~9.9B coins). */
    char digits[18];
    uint8_t i;
    for (i = 0; i < 18u; i++)
        digits[i] = (char)('0' + div64_10(&hi, &lo));

    /* digits[0..7]  = 8 fractional decimal places (LSB first)
       digits[8..17] = up to 10 whole-coin digits (LSB first) */

    /* Write whole part (digits[17..8] reversed, skip leading zeros) */
    uint8_t olen = 0;
    uint8_t leading = 1;
    for (i = 18u; i > 8u; i--) {
        if (leading && digits[i-1] == '0') continue;
        leading = 0;
        out[olen++] = digits[i-1];
    }
    if (olen == 0) out[olen++] = '0';

    out[olen++] = '.';

    /* Write 3 fractional digits (digits[7..5] reversed = 3 most-significant frac digits) */
    out[olen++] = digits[7];
    out[olen++] = digits[6];
    out[olen++] = digits[5];

    /* Strip trailing zeros, keep at least 1 */
    while (olen > 0 && out[olen-1] == '0' && out[olen-2] != '.') olen--;

    out[olen] = '\0';
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

    // input count — store count only, skip all input data
    uint32_t n_in = read_varint(tx, tx_len, &pos, &ok);
    if (!ok || n_in == 0 || n_in > 255u) return 0;
    out->n_inputs = (uint8_t)n_in;

    for (uint8_t i = 0; i < (uint8_t)n_in; i++) {
        // txid (32 bytes) — skip
        if (pos + 32 > tx_len) return 0;
        pos += 32;
        // vout (4 bytes) — skip
        if (pos + 4 > tx_len) return 0;
        pos += 4;
        // scriptSig — skip
        uint32_t sslen = read_varint(tx, tx_len, &pos, &ok);
        if (!ok || pos + sslen > tx_len) return 0;
        pos += (uint16_t)sslen;
        // sequence (4 bytes) — skip
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
