#!/usr/bin/env python3
"""
patch_rom_crc32.py
Scans ROM to auto-detect last_used_bank, then injects an 8-byte trailer:
checksum u16, last_used u16, CRC32 u32. last_used is 16-bit so an 8MB
image can name a bank past 255. Eight trailer bytes stay under the
varied-byte threshold and are not themselves a used bank.

EverDrive-GB loads 7MB of an 8MB image (banks 0..447). The old trailer
at the end of bank 511 is in the megabyte the cart does not keep.
ROM_TRAILER_BANK in src/bitrot_rom.h is the same bank as TRAILER_BANK.
"""
import struct
import sys
from collections import Counter

# First 8 bytes of this bank. Must match ROM_TRAILER_BANK.
TRAILER_BANK = 400
# Banks at or above this are outside the 7MB the EverDrive stores.
EVERDRIVE_BANK_LIMIT = 448
BANK_SIZE = 0x4000

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
    """Little-endian word sum. Matches src/bitrot_sum.s (pop bc; add hl, bc)."""
    n = len(data)
    if n & 1:
        raise SystemExit("checksum coverage is not an even number of bytes")
    if n == 0:
        return 0
    words = struct.unpack("<%dH" % (n // 2), data)
    return sum(words) & 0xFFFF

def find_last_used_bank(data: bytearray, bank_size: int = 0x4000, varied_threshold: int = 8) -> int:
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
    # 0x0201 + 0x0403 = 0x0604. Locks the word-sum against the asm.
    if simple_checksum(bytearray(b"\x01\x02\x03\x04")) != 0x0604:
        raise SystemExit("word-sum self-check failed")

    if len(sys.argv) != 2:
        print("Usage: python3 patch_rom_crc32.py <rom_file.gb>")
        sys.exit(1)

    rom_path = sys.argv[1]

    with open(rom_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 8:
        print("Error: ROM too small")
        sys.exit(1)

    last_used_bank = find_last_used_bank(data)
    if last_used_bank > 0xFFFF:
        print("Error: last_used_bank does not fit in 16 bits")
        sys.exit(1)
    if TRAILER_BANK >= EVERDRIVE_BANK_LIMIT:
        print(f"Error: trailer bank {TRAILER_BANK} is outside the EverDrive 7MB")
        sys.exit(1)
    if last_used_bank >= TRAILER_BANK:
        print(f"Error: last_used_bank {last_used_bank} reaches the trailer at bank {TRAILER_BANK}")
        sys.exit(1)

    print(f"Detected last_used_bank: {last_used_bank}")

    covered_size = (last_used_bank + 1) * BANK_SIZE
    trailer_offset = TRAILER_BANK * BANK_SIZE
    if trailer_offset + 8 > len(data) or covered_size > trailer_offset:
        print("Error: ROM is shorter than the trailer bank")
        sys.exit(1)
    if data[trailer_offset:trailer_offset + 8] != b"\xff" * 8:
        print(f"Error: trailer slot at bank {TRAILER_BANK} is not empty")
        sys.exit(1)

    # Checksum and CRC cover banks 0..last_used. The trailer is outside that.
    computed_checksum = simple_checksum(data[:covered_size])
    computed_crc = crc32(data[:covered_size])

    # Inject checksum (LE), last_used_bank (LE u16), CRC (LE)
    data[trailer_offset:trailer_offset + 2] = computed_checksum.to_bytes(2, "little")
    data[trailer_offset + 2:trailer_offset + 4] = last_used_bank.to_bytes(2, "little")
    data[trailer_offset + 4:trailer_offset + 8] = computed_crc.to_bytes(4, "little")

    # The old end-of-image slot is in the megabyte the cart drops.
    if len(data) == 512 * BANK_SIZE:
        data[-8:] = b"\xff" * 8

    with open(rom_path, "wb") as f:
        f.write(data)

    print(f"Injected checksum 0x{computed_checksum:04X} + last_used_bank={last_used_bank} + CRC32 0x{computed_crc:08X} at 0x{trailer_offset:X} ({rom_path})")

if __name__ == "__main__":
    main()
