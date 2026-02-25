#pragma bank 0
#include "bitrot_rom.h"
#include <gb/gb.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// CRC32 that continues from an existing CRC (for incremental hashing)
static void crc32_update(uint32_t *crc, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        *crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (*crc & 1)
                *crc = (*crc >> 1) ^ 0xEDB88320UL;
            else
                *crc >>= 1;
        }
    }
}

// Slow, but more robust integrity check. (CRC32)
bool rom_verify_integrity(void)
{
    uint8_t saved_bank = _current_bank;
    uint32_t stored_crc;
    uint8_t last_used_bank;

    // Read stored CRC (last 4 bytes) and last_used_bank (byte before it)
    uint8_t rom_size_code = *(const uint8_t *)0x0148;
    uint32_t num_banks = 2UL << rom_size_code;
    uint8_t last_bank = (uint8_t)(num_banks - 1);

    SWITCH_ROM(last_bank);
    last_used_bank = *(const uint8_t *)0x7FFB;  // the rom byte
    memcpy(&stored_crc, (void *)0x7FFC, 4);     // last 4 bytes

    // === Compute CRC32 of ROM up to last_used_bank ===
    uint32_t computed_crc = 0xFFFFFFFF;

    // Bank 0 (always full)
    SWITCH_ROM(0);
    crc32_update(&computed_crc, (const uint8_t *)0x0000, 0x4000);

    // Banks 1 .. last_used_bank (always full, since we always skip the slot bank)
    for (uint8_t b = 1; b <= last_used_bank; b++) {
        SWITCH_ROM(b);
        crc32_update(&computed_crc, (const uint8_t *)0x4000, 0x4000);
    }

    computed_crc = ~computed_crc; // final XOR (matches python)

    SWITCH_ROM(saved_bank);

    return computed_crc == stored_crc;
}

// Simple checksum for boot-time integrity check.
bool quick_rom_verify_integrity(void) {
    uint8_t saved_bank = _current_bank;
    uint32_t num_banks;
    uint8_t last_bank;
    uint8_t last_used_bank;
    uint16_t stored_checksum;

    uint8_t rom_size_code = *(const uint8_t *)0x0148;
    num_banks = 2UL << rom_size_code;
    last_bank = (uint8_t)(num_banks - 1);

    SWITCH_ROM(last_bank);
    // Read stored values from end of last bank
    last_used_bank = *(const uint8_t *)0x7FFB;

    // Checksum is 2 bytes before that (0x7FF9-0x7FFA, little-endian)
    stored_checksum = *(const uint8_t *)0x7FF9 | (*(const uint8_t *)0x7FFA << 8);

    // === Compute simple additive checksum of ROM up to last_used_bank ===
    uint16_t computed_checksum = 0;

    // Bank 0 (always full)
    SWITCH_ROM(0);
    const uint8_t *ptr = (const uint8_t *)0x0000;

    for (uint16_t i = 0; i < 0x4000; i++) {
        computed_checksum += *ptr++;
    }

    // Banks 1 .. last_used_bank (always full)
    for (uint8_t b = 1; b <= last_used_bank; b++) {
        SWITCH_ROM(b);
        ptr = (const uint8_t *)0x4000;
        for (uint16_t i = 0; i < 0x4000; i++) {
            computed_checksum += *ptr++;
        }
    }

    SWITCH_ROM(saved_bank);

    return computed_checksum == stored_checksum;
}