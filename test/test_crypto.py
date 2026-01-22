#!/usr/bin/env python3
"""
Test script that generates random mnemonics, computes expected addresses,
runs the C app, and compares results for statistical validation.
"""

import subprocess
import sys
import os
from mnemonic import Mnemonic
from bip32utils import BIP32Key
import hashlib
import base58
import random

def generate_random_mnemonic():
    """Generate a random 12-word BIP39 mnemonic"""
    mnemo = Mnemonic("english")
    entropy = os.urandom(16)  # 128 bits
    return mnemo.to_mnemonic(entropy)

def python_mnemonic_to_address(mnemonic):
    """Compute Dogecoin address using Python (reference implementation)"""
    mnemo = Mnemonic("english")
    seed = mnemo.to_seed(mnemonic, passphrase="")

    # BIP32 derivation: m/44'/3'/0'/0/0
    bip32_root = BIP32Key.fromEntropy(seed)
    bip32_child = (bip32_root
                   .ChildKey(44 + 0x80000000)
                   .ChildKey(3 + 0x80000000)
                   .ChildKey(0 + 0x80000000)
                   .ChildKey(0)
                   .ChildKey(0))

    pubkey = bip32_child.PublicKey()

    # P2PKH address generation
    sha256_hash = hashlib.sha256(pubkey).digest()
    ripemd160 = hashlib.new('ripemd160')
    ripemd160.update(sha256_hash)
    hash160 = ripemd160.digest()

    versioned = b'\x1e' + hash160  # Dogecoin mainnet
    checksum = hashlib.sha256(hashlib.sha256(versioned).digest()).digest()[:4]
    address_bytes = versioned + checksum

    return base58.b58encode(address_bytes).decode('utf-8')

def c_mnemonic_to_address(mnemonic):
    """Run C app to compute address"""
    try:
        result = subprocess.run(
            ['./test/mnemonic_to_address', mnemonic],
            capture_output=True,
            text=True,
            timeout=30
        )
        if result.returncode == 0:
            return result.stdout.strip()
        else:
            print(f"C app failed: {result.stderr}")
            return None
    except subprocess.TimeoutExpired:
        print("C app timed out")
        return None
    except FileNotFoundError:
        print("C app not found - compile it first")
        return None

def main():
    if len(sys.argv) != 2:
        print("Usage: python test_crypto.py <num_tests>")
        sys.exit(1)

    num_tests = int(sys.argv[1])
    passed = 0

    print(f"Running {num_tests} crypto validation tests...")
    print("=" * 50)

    for i in range(num_tests):
        # Generate random mnemonic
        mnemonic = generate_random_mnemonic()

        # Compute expected address with Python
        expected = python_mnemonic_to_address(mnemonic)

        # Compute address with C app
        actual = c_mnemonic_to_address(mnemonic)

        if actual is None:
            continue


        if expected == actual:
            passed += 1

    print(f"Results: {passed}/{num_tests} tests passed ({100*passed/num_tests:.1f}%)")

    if passed == num_tests:
        print("✓ All tests passed! Crypto implementation is correct.")
        return 0
    else:
        print("✗ Some tests failed. Check implementation.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
