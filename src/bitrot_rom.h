#ifndef BITROT_ROM_H
#define BITROT_ROM_H

#include <stdbool.h>
#include <stdint.h>

/* EverDrive-GB keeps the last 1MB of an 8MB image, so bank 511 is not
   readable. The trailer is the first 8 bytes of this bank, at CPU
   0x4000: checksum, last_used, crc. It stays above last_used and
   inside the 7MB the cart loads. tools/patch_bitrot.py uses this bank. */
#define ROM_TRAILER_BANK 400u

/* A null checksum or crc pointer skips that field. */
void read_trailer(uint16_t *last_used, uint16_t *checksum, uint32_t *crc);

/* False when last_used would walk into the trailer or off the end of
   the ROM. Callers must not scan that span. */
bool rom_last_in_range(uint16_t last_used);

/* Boot word sum, one half-bank per call. first_half reloads boot_ptr
   to the start of that bank. The splash calls this between frames.
   Two calls, first half then second, cover one full bank. */
void rom_sum_slice(uint16_t bank, uint8_t first_half);

/* CRC32 over one bank. rom_crc_open loads the table into SRAM.
   rom_crc_close wipes that table. The running state stays in crc_state. */
void rom_crc_open(void);
void rom_crc_bank(uint16_t bank);
void rom_crc_close(void);

#endif