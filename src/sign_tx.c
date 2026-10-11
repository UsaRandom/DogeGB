#pragma bank 15

#include "sign_tx.h"
#include "sign_mem.h"
#include "ecdsa.h"
#include "pbkdf2.h"
#include "ripemd160.h"
#include "progress.h"
#include "wallet_sram.h"

#include <stddef.h>
#include <string.h>

extern uint8_t pin_hash[32];

/* Walk the encrypted slot once and keep only the key. The rest of the
   slot, including the mnemonic, is not stored. SRAM is left as it was. */
static uint8_t load_slot_key(uint8_t slot, uint8_t priv[32], uint8_t pub[33]) {
    uint16_t i;
    uint16_t lcg;
    uint16_t len;
    uint16_t priv_at;
    uint16_t pub_at;
    volatile uint8_t *data;
    const uint8_t *key;

    if (slot < 1u || slot > MAX_SLOTS) return 0;
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (slots[slot - 1u].used != SLOT_USED_MARKER) {
        DISABLE_RAM_MBC5;
        return 0;
    }
    key = pin_hash;
    lcg = (uint16_t)((uint16_t)key[0] | ((uint16_t)key[1] << 8));
    data = (volatile uint8_t *)slots[slot - 1u].address;
    len = (uint16_t)(sizeof(SaveSlot) - offsetof(SaveSlot, address));
    priv_at = (uint16_t)(offsetof(SaveSlot, private_key) - offsetof(SaveSlot, address));
    pub_at = (uint16_t)(offsetof(SaveSlot, public_key) - offsetof(SaveSlot, address));
    for (i = 0; i < len; i++) {
        uint8_t b = (uint8_t)(data[i] ^ key[lcg & 0x1Fu] ^ (uint8_t)(lcg >> 8));
        lcg = (uint16_t)(lcg * 5u + 1u);
        if (i >= priv_at && i < (uint16_t)(priv_at + 32u))
            priv[(uint16_t)(i - priv_at)] = b;
        else if (i >= pub_at && i < (uint16_t)(pub_at + 33u))
            pub[(uint16_t)(i - pub_at)] = b;
    }
    DISABLE_RAM_MBC5;
    return 1;
}

uint8_t sign_payment(uint8_t slot, uint8_t n_in, const uint8_t *txid,
                     const uint16_t *vout, uint8_t has_change, uint8_t dest_type,
                     const uint8_t pay[8], const uint8_t change[8],
                     const uint8_t dest[20], uint16_t *blob_len) BANKED {
    uint16_t len = 0;
    uint16_t cap;
    uint16_t used;
    uint8_t *base;
    uint8_t *tail;
    SignMem *m;
    SighashIn in;
    uint8_t i;

    if (blob_len) *blob_len = 0;
    base = kdf_scratch(&len);
    if (len < sizeof(SignMem) || n_in < 1u || n_in > 4u) return 0;
    m = (SignMem *)base;
    tail = base + sizeof(SignMem);
    cap = (uint16_t)(len - sizeof(SignMem));
    memset(m, 0, sizeof(SignMem));
    if (cap < 33u || !load_slot_key(slot, m->priv, m->pub)) {
        memset(base, 0, len);
        return 0;
    }

    sha256_init(&m->ctx);
    sha256_update(&m->ctx, m->pub, 33);
    sha256_final(&m->ctx, m->hash);
    ripemd160(m->hash, 32, m->h160);
    add_progress(WEIGHT_HASH160);
    /* The first signature overwrites pub with k*G. Keep the real key. */
    memcpy(tail, m->pub, 33);
    used = 33;

    in.n_in = n_in;
    in.has_change = has_change;
    in.dest_type = dest_type;
    in.txid = txid;
    in.vout = vout;
    in.pay = pay;
    in.change = change;
    in.dest = dest;
    for (i = 0; i < n_in; i++) {
        in.which = i;
        if (!sighash_one(m, &in) || !ecdsa_sign_mem(m) ||
            m->sig_len == 0u || m->sig_len > 73u ||
            (uint16_t)(used + m->sig_len) > cap) {
            memset(base, 0, len);
            return 0;
        }
        memcpy(tail + used, m->sig, m->sig_len);
        used = (uint16_t)(used + m->sig_len);
    }
    memset(m, 0, sizeof(SignMem));
    if (blob_len) *blob_len = used;
    return 1;
}
