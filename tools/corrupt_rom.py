#!/usr/bin/env python3
"""
corrupt_rom.py
Flips exactly ONE random bit **only inside the used ROM space** (banks 0 to last_used_bank),
so it always triggers the integrity check failure when testing bitrot protection.

The used range is detected the same way as in patch_rom_crc32.py.

Usage:
    python3 corrupt_rom.py build/DogeGB.gb
    → creates build/DogeGB_corrupted.gb
    python3 corrupt_rom.py build/DogeGB.gb my_test.rom
    → creates my_test.rom with the flip
"""

import sys
import random
import os
from collections import Counter

def find_last_used_bank(data: bytearray, bank_size: int = 0x4000, varied_threshold: int = 7) -> int:
    """
    Highest bank where (bank_size - most_common_byte_count) > threshold
    """
    num_banks = len(data) // bank_size
    for bank in range(num_banks - 1, -1, -1):
        start = bank * bank_size
        end = start + bank_size
        bank_data = data[start:end]
        if not bank_data:
            continue
        counter = Counter(bank_data)
        if counter:
            mode_count = counter.most_common(1)[0][1]
            varied_count = len(bank_data) - mode_count
            if varied_count > varied_threshold:
                return bank
    return 0  # fallback


def main():
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print("Usage:")
        print("  python3 corrupt_rom.py <input.rom> [output.rom]")
        print("  (if no output, appends _corrupted.gb)")
        sys.exit(1)

    input_path = sys.argv[1]

    if len(sys.argv) == 3:
        output_path = sys.argv[2]
    else:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_corrupted{ext}"

    # Read the whole ROM
    with open(input_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 64:
        print("Error: ROM too small to corrupt sensibly")
        sys.exit(1)

    last_used_bank = find_last_used_bank(data)
    bank_size = 0x4000
    max_offset = (last_used_bank + 1) * bank_size   

    if max_offset <= 0 or max_offset > len(data):
        print("Error: Could not detect valid used space")
        sys.exit(1)

    print(f"Detected used space: banks 0–{last_used_bank} (0x{max_offset:06X} bytes)")

    # Pick random byte **only inside used space** (0 .. max_offset-1)
    byte_idx = random.randint(0, max_offset - 1)
    bit_idx = random.randint(0, 7)  # 0=LSB, 7=MSB

    # Flip the bit
    original_byte = data[byte_idx]
    mask = 1 << bit_idx
    data[byte_idx] ^= mask
    new_byte = data[byte_idx]

    # Write corrupted file
    with open(output_path, "wb") as f:
        f.write(data)

    print(f"Corrupted ROM created (used space only):")
    print(f"  Input : {input_path}")
    print(f"  Output: {output_path}")
    print(f"  Flipped byte 0x{byte_idx:06X} (bit {bit_idx})  ← inside bank {byte_idx // bank_size}")
    print(f"  Original byte: 0x{original_byte:02X}")
    print(f"  New byte     : 0x{new_byte:02X}")
    print(f"\nFlash {output_path} → should trigger corruption detection!")


if __name__ == "__main__":
    main()