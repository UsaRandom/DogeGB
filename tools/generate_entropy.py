#!/usr/bin/env python3
"""
generate_entropy.py

Generates fresh random entropy pool + build salt and writes them
as C arrays into build/entropy_data.h and build/entropy_data.c
"""

import os
import secrets
import textwrap

ENTROPY_POOL_SIZE = 512
SALT_SIZE = 128
OUTPUT_DIR = "src/crypto"

HEADER_FILE = "entropy_data.h"
C_FILE = "entropy_data.c"


def bytes_to_c_array(data: bytes) -> str:
    """Convert bytes → clean C array lines without trailing commas"""
    hex_bytes = [f"0x{b:02x}" for b in data]
    
    lines = []
    current_line = []
    current_length = 0
    
    for i, val in enumerate(hex_bytes):
        if current_line:
            addition = f", {val}"
        else:
            addition = val
        
        if current_length + len(addition) > 78 and current_line:
            lines.append("    " + ", ".join(current_line) + ",")
            current_line = [val]
            current_length = len(val)
        else:
            current_line.append(val)
            current_length += len(addition)
    
    if current_line:
        lines.append("    " + ", ".join(current_line))
    
    return "\n".join(lines)


def main():
    # Create output directory
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Generate cryptographically strong random bytes
    entropy_pool = secrets.token_bytes(ENTROPY_POOL_SIZE)
    build_salt   = secrets.token_bytes(SALT_SIZE)

    # Convert to nice C array strings
    pool_array_str = bytes_to_c_array(entropy_pool)
    salt_array_str = bytes_to_c_array(build_salt)

    # ──────────────────────────────────────
    # Header file
    # ──────────────────────────────────────
    header_content = f"""\
#ifndef ENTROPY_DATA_H
#define ENTROPY_DATA_H

#include <stdint.h>

#define ENTROPY_POOL_SIZE   {ENTROPY_POOL_SIZE}
#define BUILD_SALT_SIZE     {SALT_SIZE}

#endif // ENTROPY_DATA_H
"""

    # ──────────────────────────────────────
    # C file with the actual arrays
    # ──────────────────────────────────────
    c_content = f"""\
#include "{HEADER_FILE}"

uint8_t entropy_pool[ENTROPY_POOL_SIZE] = {{
    {pool_array_str}
}};

uint8_t build_salt[BUILD_SALT_SIZE] = {{
    {salt_array_str}
}};
"""

    # Write files
    with open(os.path.join(OUTPUT_DIR, HEADER_FILE), "w", encoding="utf-8") as f:
        f.write(header_content)

    with open(os.path.join(OUTPUT_DIR, C_FILE), "w", encoding="utf-8") as f:
        f.write(c_content.lstrip())

    print(f"Generated fresh entropy data:")
    print(f"  {ENTROPY_POOL_SIZE} bytes pool  →  {OUTPUT_DIR}/{HEADER_FILE}")
    print(f"  {SALT_SIZE} bytes salt    →  {OUTPUT_DIR}/{C_FILE}")


if __name__ == "__main__":
    main()