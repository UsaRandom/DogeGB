#include <gb/gb.h>
#include <stdbool.h>

void add_progress(uint16_t weight) BANKED;

void show_progress_page() BANKED;

void prepare_rom_check_bar(void) BANKED;
void update_progress(uint8_t progress) BANKED;
void use_coin_bar_color(void) BANKED;

/* Boot check: label on row 7, bar on rows 9-10. */
#define BOOT_BAR_Y 9
#define BOOT_CHECK_TEXT_Y 7

/* Address gen has no title band. Title, timer, and bar are one
   centered stack with a blank row between each. */
#define ADDR_TITLE_Y 6
#define ADDR_ETA_Y 8

/* CRC32 over the same banks as the boot word sum. */
bool rom_verify_integrity(void) BANKED;

/* ETA, and a coin on each side of the timer. spread is extra pixels
   between each coin and the text. Address gen uses 0. Signing uses 8. */
void progress_ride_begin(uint8_t spread) BANKED;
void progress_ride_tick(unsigned long done, unsigned long total) BANKED;
void progress_ride_end(void) BANKED;

/* The first address generation derives the known wallet in these
   buffers, then the caller's mnemonic. Later calls return at once.
   Returns 0 when the known addresses do not match. */
uint8_t run_known_addr_check(uint8_t *seed, uint8_t *priv, uint8_t *pub,
                             char *doge, char *pepe, char *bells) BANKED;

/* Weights are GBC milliseconds.
   One PBKDF2 iteration is 263 ms (2048 of them).
   One comb tick is 47 ms (256 per key, three keys).
   One inverse field op is 50 ms (270 per key, three keys).
   Hash160 is 285 ms. Each address encoding is 586 ms.
   Each address check is 293 ms. Three of each.
   One CRC bank is 235 ms.
   The address page budgets the CRC plus the address ticks.
   The first address generation adds a second derivation.
   The boot splash does not use these weights. */
#define WEIGHT_PBKDF2       263u
#define WEIGHT_SECP256k1     47u
#define WEIGHT_INV           50u
#define WEIGHT_HASH160      285u
#define WEIGHT_ADDR_ENCODE  586u
#define WEIGHT_ADDR_CHECK   293u
#define WEIGHT_CRC_BANK     235u
#define INVERSE_OPS         270u

#define ADDRESS_WORK_UL (2048UL * WEIGHT_PBKDF2 \
    + 3UL * 256UL * WEIGHT_SECP256k1 \
    + 3UL * INVERSE_OPS * WEIGHT_INV \
    + (unsigned long)WEIGHT_HASH160 \
    + 3UL * WEIGHT_ADDR_ENCODE \
    + 3UL * WEIGHT_ADDR_CHECK)

/* Signing page, measured 2026-10-11 with interrupts off.
   One sighash is 569 ms. RFC 6979 is 2955 ms.
   One scalar inverse is 1976 ms. One mod-N multiply is 1432 ms,
   split across 32 ticks of 45 ms. Two multiplies per signature.
   Pubkey ticks stay inside secp256k1_pubkey.
   Hash160 of the stored key is the existing 285 ms, once. */
#define WEIGHT_SIGN_HASH    569u
#define WEIGHT_SIGN_RFC     2955u
#define WEIGHT_SIGN_INV     1976u
#define WEIGHT_SIGN_BIT     45u
#define SIGN_REDUCE_TICKS   32UL
#define SIGN_PUB_MS (256UL * WEIGHT_SECP256k1 + (unsigned long)INVERSE_OPS * WEIGHT_INV)
#define SIGN_PER_INPUT_MS (SIGN_PUB_MS \
    + (unsigned long)WEIGHT_SIGN_HASH \
    + (unsigned long)WEIGHT_SIGN_RFC \
    + (unsigned long)WEIGHT_SIGN_INV \
    + 2UL * SIGN_REDUCE_TICKS * (unsigned long)WEIGHT_SIGN_BIT)
#define SIGN_FIXED_MS ((unsigned long)WEIGHT_HASH160)

void show_sign_progress(uint8_t n_in) BANKED;
