#pragma bank 6

#include "tx_signer.h"
#include "ecdsa.h"
#include "sha256.h"
#include "ripemd160.h"

#include <string.h>
#include <stdint.h>

// Maximum unsigned tx size we'll handle
#define MAX_TX_LEN      1280
#define MAX_INPUTS      8
#define MAX_SCRIPT_LEN  256

// Static buffers (GBC has no dynamic allocation)
static uint8_t s_sighash_buf[MAX_TX_LEN + 32]; // preimage + SIGHASH_ALL word
static uint8_t s_sigs[MAX_INPUTS][73];
static uint8_t s_sig_lens[MAX_INPUTS];

// ---- helpers ----

static uint8_t write_varint(uint8_t *out, uint32_t val) {
    if (val < 0xFD) { out[0] = (uint8_t)val; return 1; }
    if (val <= 0xFFFF) {
        out[0] = 0xFD; out[1] = val & 0xFF; out[2] = (val >> 8) & 0xFF; return 3;
    }
    // 4-byte varint (won't happen in practice for our tx sizes)
    out[0] = 0xFE;
    out[1] = val & 0xFF; out[2] = (val >> 8) & 0xFF;
    out[3] = (val >> 16) & 0xFF; out[4] = (val >> 24) & 0xFF;
    return 5;
}

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
    *ok = 0; return 0;
}

// Derive P2PKH scriptPubKey from compressed public key (25 bytes)
// script: 76 a9 14 [hash160 20 bytes] 88 ac
static void pubkey_to_p2pkh_script(const uint8_t *pubkey, uint8_t *script) {
    SHA256_CTX ctx;
    uint8_t sha_hash[32];
    uint8_t hash160[20];

    sha256_init(&ctx);
    sha256_update(&ctx, pubkey, 33);
    sha256_final(&ctx, sha_hash);
    ripemd160(sha_hash, 32, hash160);

    script[0] = 0x76; script[1] = 0xa9; script[2] = 0x14;
    memcpy(script + 3, hash160, 20);
    script[23] = 0x88; script[24] = 0xac;
}

// Build SIGHASH_ALL preimage for input at input_idx.
// signing_script: the scriptPubKey of the UTXO being spent (for P2PKH = 25 bytes).
// Returns total preimage length or 0 on error.
static uint16_t build_sighash_preimage(const uint8_t *tx, uint16_t tx_len,
                                        uint8_t input_idx,
                                        const uint8_t *signing_script, uint8_t slen,
                                        uint8_t *out, uint16_t out_max) {
    uint16_t pos = 0, wpos = 0;
    uint8_t ok = 1;

    // version
    if (pos + 4 > tx_len || wpos + 4 > out_max) return 0;
    memcpy(out + wpos, tx + pos, 4); wpos += 4; pos += 4;

    // input count
    uint32_t n_in = read_varint(tx, tx_len, &pos, &ok);
    if (!ok) return 0;
    uint8_t vi_len = write_varint(out + wpos, n_in);
    wpos += vi_len;

    for (uint8_t i = 0; i < (uint8_t)n_in; i++) {
        // txid (32) + vout (4)
        if (pos + 36 > tx_len || wpos + 36 > out_max) return 0;
        memcpy(out + wpos, tx + pos, 36); wpos += 36; pos += 36;

        // scriptSig length from original tx
        uint32_t orig_sslen = read_varint(tx, tx_len, &pos, &ok);
        if (!ok || pos + orig_sslen > tx_len) return 0;
        pos += (uint16_t)orig_sslen; // skip original scriptSig

        if (i == input_idx) {
            // Replace with signing script
            if (wpos + 1 + slen > out_max) return 0;
            wpos += write_varint(out + wpos, slen);
            memcpy(out + wpos, signing_script, slen); wpos += slen;
        } else {
            // Empty scriptSig
            if (wpos + 1 > out_max) return 0;
            out[wpos++] = 0x00;
        }

        // sequence (4 bytes)
        if (pos + 4 > tx_len || wpos + 4 > out_max) return 0;
        memcpy(out + wpos, tx + pos, 4); wpos += 4; pos += 4;
    }

    // outputs
    uint32_t n_out = read_varint(tx, tx_len, &pos, &ok);
    if (!ok) return 0;
    wpos += write_varint(out + wpos, n_out);

    for (uint32_t i = 0; i < n_out; i++) {
        // value (8 bytes)
        if (pos + 8 > tx_len || wpos + 8 > out_max) return 0;
        memcpy(out + wpos, tx + pos, 8); wpos += 8; pos += 8;

        uint32_t spklen = read_varint(tx, tx_len, &pos, &ok);
        if (!ok || pos + spklen > tx_len) return 0;
        if (wpos + 1 + spklen > out_max) return 0;
        wpos += write_varint(out + wpos, spklen);
        memcpy(out + wpos, tx + pos, (uint16_t)spklen);
        wpos += (uint16_t)spklen; pos += (uint16_t)spklen;
    }

    // locktime (4 bytes)
    if (pos + 4 > tx_len || wpos + 4 > out_max) return 0;
    memcpy(out + wpos, tx + pos, 4); wpos += 4; pos += 4;

    // SIGHASH_ALL (4 bytes LE: 01 00 00 00)
    if (wpos + 4 > out_max) return 0;
    out[wpos] = 0x01; out[wpos+1] = 0; out[wpos+2] = 0; out[wpos+3] = 0;
    wpos += 4;

    return wpos;
}

// ---- Public API ----

uint8_t sign_tx(const uint8_t *unsigned_tx, uint16_t tx_len,
                const uint8_t privkey[32], const uint8_t pubkey[33],
                uint8_t *out, uint16_t out_max, uint16_t *out_len) BANKED {
    SHA256_CTX ctx;
    uint8_t hash1[32], hash2[32];
    uint8_t signing_script[25]; // P2PKH is always 25 bytes
    uint16_t pos;
    uint8_t ok = 1;

    if (tx_len > MAX_TX_LEN) return 0;

    // Derive P2PKH locking script from our pubkey
    pubkey_to_p2pkh_script(pubkey, signing_script);

    // Parse input count
    pos = 4; // skip version
    uint32_t n_in = read_varint(unsigned_tx, tx_len, &pos, &ok);
    if (!ok || n_in == 0 || n_in > MAX_INPUTS) return 0;

    // Sign each input
    for (uint8_t i = 0; i < (uint8_t)n_in; i++) {
        uint16_t preimage_len = build_sighash_preimage(
            unsigned_tx, tx_len, i,
            signing_script, 25,
            s_sighash_buf, (uint16_t)sizeof(s_sighash_buf));
        if (!preimage_len) return 0;

        // double-SHA256
        sha256_init(&ctx);
        sha256_update(&ctx, s_sighash_buf, preimage_len);
        sha256_final(&ctx, hash1);
        sha256_init(&ctx);
        sha256_update(&ctx, hash1, 32);
        sha256_final(&ctx, hash2);

        s_sig_lens[i] = ecdsa_sign(privkey, hash2, s_sigs[i]);
        if (s_sig_lens[i] == 0) return 0;
    }

    // ---- Assemble signed transaction ----
    // Re-parse unsigned tx, inserting scriptSig for each input
    uint16_t wpos = 0;
    pos = 0;

    // version
    if (wpos + 4 > out_max) return 0;
    memcpy(out + wpos, unsigned_tx + pos, 4); wpos += 4; pos += 4;

    // re-read n_in (pos is now after version)
    ok = 1;
    read_varint(unsigned_tx, tx_len, &pos, &ok); // advance past varint
    // write n_in
    if (wpos + 1 > out_max) return 0;
    wpos += write_varint(out + wpos, n_in);

    for (uint8_t i = 0; i < (uint8_t)n_in; i++) {
        // txid + vout (36 bytes)
        if (pos + 36 > tx_len || wpos + 36 > out_max) return 0;
        memcpy(out + wpos, unsigned_tx + pos, 36); wpos += 36; pos += 36;

        // skip original (empty) scriptSig
        uint32_t orig_sslen = read_varint(unsigned_tx, tx_len, &pos, &ok);
        if (!ok || pos + orig_sslen > tx_len) return 0;
        pos += (uint16_t)orig_sslen;

        // Write new scriptSig: <sig_len> <DER sig + 0x01> <0x21> <33-byte pubkey>
        uint8_t sig_len = s_sig_lens[i]; // includes SIGHASH_ALL byte already
        uint8_t scriptsig_len = 1 + sig_len + 1 + 33; // push_sig + sig + push_pub + pub
        if (wpos + 1 + scriptsig_len > out_max) return 0;
        wpos += write_varint(out + wpos, scriptsig_len);
        out[wpos++] = sig_len;
        memcpy(out + wpos, s_sigs[i], sig_len); wpos += sig_len;
        out[wpos++] = 0x21; // push 33 bytes
        memcpy(out + wpos, pubkey, 33); wpos += 33;

        // sequence (4 bytes)
        if (pos + 4 > tx_len || wpos + 4 > out_max) return 0;
        memcpy(out + wpos, unsigned_tx + pos, 4); wpos += 4; pos += 4;
    }

    // Copy outputs + locktime verbatim
    uint16_t remaining = tx_len - pos;
    if (wpos + remaining > out_max) return 0;
    memcpy(out + wpos, unsigned_tx + pos, remaining);
    wpos += remaining;

    *out_len = wpos;
    return 1;
}
