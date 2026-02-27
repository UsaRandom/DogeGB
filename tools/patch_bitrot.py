#!/usr/bin/env python3
"""
patch_rom_crc32.py
Scans ROM to auto-detect last_used_bank (highest with >40 varied bytes from mode),
then injects 2-byte checksum + 1-byte count + 4-byte CRC32 at end.
"""
import sys
from collections import Counter

def crc32(data: bytearray) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return ~crc & 0xFFFFFFFF

def simple_checksum(data: bytearray) -> int:
    checksum = 0
    for b in data:
        checksum = (checksum + b) & 0xFFFF
    return checksum

def find_last_used_bank(data: bytearray, bank_size: int = 0x4000, varied_threshold: int = 7) -> int:
    num_banks = len(data) // bank_size
    for bank in range(num_banks - 1, -1, -1):  # Start from last
        start = bank * bank_size
        end = start + bank_size
        bank_data = data[start:end]

        if not bank_data: continue

        counter = Counter(bank_data)

        if counter:
            mode_count = counter.most_common(1)[0][1]
            varied_count = len(bank_data) - mode_count
            if varied_count > varied_threshold:
                return bank
            
    return 0  # Fallback if all empty (unlikely)

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 patch_rom_crc32.py <rom_file.gb>")
        sys.exit(1)

    rom_path = sys.argv[1]

    with open(rom_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 7:
        print("Error: ROM too small")
        sys.exit(1)

    last_used_bank = find_last_used_bank(data)

    print(f"Detected last_used_bank: {last_used_bank}")

    bank_size = 0x4000
    covered_size = (last_used_bank + 1) * bank_size
    checksum_offset = len(data) - 7
    count_offset = len(data) - 5
    crc_offset = len(data) - 4

    # Compute checksum and CRC over covered ROM
    computed_checksum = simple_checksum(data[:covered_size])
    computed_crc = crc32(data[:covered_size])

    # Inject checksum (LE), last_used_bank, CRC (LE)
    data[checksum_offset : checksum_offset + 2] = computed_checksum.to_bytes(2, "little")
    data[count_offset] = last_used_bank
    data[crc_offset : crc_offset + 4] = computed_crc.to_bytes(4, "little")

    with open(rom_path, "wb") as f:
        f.write(data)

    print(f"Injected checksum 0x{computed_checksum.to_bytes(2, 'little').hex().upper()} + last_used_bank={last_used_bank} + CRC32 at 0x{checksum_offset:X} ({rom_path})")

if __name__ == "__main__":
    main()