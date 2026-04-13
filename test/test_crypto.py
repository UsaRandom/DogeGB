#!/usr/bin/env python3
"""
Test script that generates random mnemonics, computes expected addresses,
generates random transactions, runs the C apps (address and signing), 
and compares results for statistical validation.
"""

import argparse
import subprocess
import sys
import os
import hashlib
import hmac
import struct
import random
import base58
from mnemonic import Mnemonic
from bip32utils import BIP32Key

# ---------------------------------------------------------------------------
# secp256k1 constants & Math (for ECDSA signing & random TX generation)
# ---------------------------------------------------------------------------
P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
G = (Gx, Gy)

def modinv(a, m):
    return pow(a, m - 2, m)

def point_add(P1, P2):
    if P1 is None: return P2
    if P2 is None: return P1
    if P1[0] == P2[0]:
        if P1[1] != P2[1]: return None
        return point_double(P1)
    lam = (P2[1] - P1[1]) * modinv(P2[0] - P1[0], P) % P
    x3 = (lam * lam - P1[0] - P2[0]) % P
    y3 = (lam * (P1[0] - x3) - P1[1]) % P
    return (x3, y3)

def point_double(pt):
    if pt is None: return None
    lam = (3 * pt[0] * pt[0]) * modinv(2 * pt[1], P) % P
    x3 = (lam * lam - 2 * pt[0]) % P
    y3 = (lam * (pt[0] - x3) - pt[1]) % P
    return (x3, y3)

def scalar_mult(k, pt):
    result = None
    addend = pt
    while k:
        if k & 1: result = point_add(result, addend)
        addend = point_double(addend)
        k >>= 1
    return result

def privkey_to_pubkey(priv_bytes):
    k = int.from_bytes(priv_bytes, 'big')
    pt = scalar_mult(k, G)
    prefix = 0x02 if pt[1] % 2 == 0 else 0x03
    return bytes([prefix]) + pt[0].to_bytes(32, 'big')

# ---------------------------------------------------------------------------
# RFC 6979 deterministic k & ECDSA sign
# ---------------------------------------------------------------------------
def rfc6979_k(privkey: bytes, msg_hash: bytes) -> int:
    x = privkey
    h1 = msg_hash
    V = b'\x01' * 32
    K = b'\x00' * 32
    K = hmac.new(K, V + b'\x00' + x + h1, hashlib.sha256).digest()
    V = hmac.new(K, V, hashlib.sha256).digest()
    K = hmac.new(K, V + b'\x01' + x + h1, hashlib.sha256).digest()
    V = hmac.new(K, V, hashlib.sha256).digest()
    while True:
        V = hmac.new(K, V, hashlib.sha256).digest()
        k = int.from_bytes(V, 'big')
        if 0 < k < N:
            return k
        K = hmac.new(K, V + b'\x00', hashlib.sha256).digest()
        V = hmac.new(K, V, hashlib.sha256).digest()

def der_encode(r: int, s: int) -> bytes:
    def encode_int(n):
        b = n.to_bytes(32, 'big').lstrip(b'\x00') or b'\x00'
        if b[0] & 0x80: b = b'\x00' + b
        return bytes([0x02, len(b)]) + b
    r_enc = encode_int(r)
    s_enc = encode_int(s)
    body = r_enc + s_enc
    return bytes([0x30, len(body)]) + body + b'\x01'  # SIGHASH_ALL

def ecdsa_sign(privkey: bytes, msg_hash: bytes) -> bytes:
    z = int.from_bytes(msg_hash, 'big')
    d = int.from_bytes(privkey, 'big')
    k = rfc6979_k(privkey, msg_hash)
    pt = scalar_mult(k, G)
    r = pt[0] % N
    s = modinv(k, N) * (z + r * d) % N
    # low-s
    if s > N // 2:
        s = N - s
    return der_encode(r, s)

# ---------------------------------------------------------------------------
# Transaction building helpers
# ---------------------------------------------------------------------------
def varint(n: int) -> bytes:
    if n < 0xFD: return bytes([n])
    if n <= 0xFFFF: return b'\xfd' + struct.pack('<H', n)
    return b'\xfe' + struct.pack('<I', n)

def _read_varint(buf, pos):
    b = buf[pos]
    if b < 0xFD: return b, 1
    if b == 0xFD: return struct.unpack_from('<H', buf, pos+1)[0], 3
    return struct.unpack_from('<I', buf, pos+1)[0], 5

def make_p2pkh_script(pubkey: bytes) -> bytes:
    h = hashlib.new('ripemd160', hashlib.sha256(pubkey).digest()).digest()
    return bytes([0x76, 0xa9, 0x14]) + h + bytes([0x88, 0xac])

def make_unsigned_tx(inputs, outputs):
    tx = struct.pack('<I', 1)  # version
    tx += varint(len(inputs))
    for (txid, vout, _val) in inputs:
        tx += txid + struct.pack('<I', vout)
        tx += varint(0)  # empty scriptSig
        tx += b'\xff\xff\xff\xff'  # sequence
    tx += varint(len(outputs))
    for (value, spk) in outputs:
        tx += struct.pack('<Q', value) + varint(len(spk)) + spk
    tx += struct.pack('<I', 0)  # locktime
    return tx

# ---------------------------------------------------------------------------
# Reference signing (Python)
# ---------------------------------------------------------------------------
def sighash_all(unsigned_tx: bytes, input_idx: int, signing_script: bytes) -> bytes:
    """Compute SIGHASH_ALL double-SHA256 for one input."""
    pos = 0
    version = unsigned_tx[pos:pos+4]; pos += 4
    n_in, adv = _read_varint(unsigned_tx, pos); pos += adv
    
    inputs_raw = []
    for i in range(n_in):
        txid = unsigned_tx[pos:pos+32]; pos += 32
        vout = unsigned_tx[pos:pos+4]; pos += 4
        sslen, adv = _read_varint(unsigned_tx, pos); pos += adv
        _ss = unsigned_tx[pos:pos+sslen]; pos += sslen
        seq = unsigned_tx[pos:pos+4]; pos += 4
        inputs_raw.append((txid, vout, seq))
        
    n_out, adv = _read_varint(unsigned_tx, pos); pos += adv
    outputs_raw = []
    for _ in range(n_out):
        val = unsigned_tx[pos:pos+8]; pos += 8
        spklen, adv = _read_varint(unsigned_tx, pos); pos += adv
        spk = unsigned_tx[pos:pos+spklen]; pos += spklen
        outputs_raw.append((val, spk))
        
    locktime = unsigned_tx[pos:pos+4]
    
    preimage = version
    preimage += varint(n_in)
    for i, (txid, vout, seq) in enumerate(inputs_raw):
        preimage += txid + vout
        if i == input_idx:
            preimage += varint(len(signing_script)) + signing_script
        else:
            preimage += varint(0)
        preimage += seq
        
    preimage += varint(n_out)
    for (val, spk) in outputs_raw:
        preimage += val + varint(len(spk)) + spk
    preimage += locktime
    preimage += b'\x01\x00\x00\x00'  # SIGHASH_ALL
    
    return hashlib.sha256(hashlib.sha256(preimage).digest()).digest()

def sign_tx_python(unsigned_tx: bytes, privkey: bytes, pubkey: bytes) -> bytes:
    signing_script = make_p2pkh_script(pubkey)
    pos = 4
    n_in, adv = _read_varint(unsigned_tx, pos); pos += adv
    
    sigs = []
    for i in range(n_in):
        h = sighash_all(unsigned_tx, i, signing_script)
        sig = ecdsa_sign(privkey, h)
        sigs.append(sig)
        
    # Assemble
    pos2 = 0
    version = unsigned_tx[0:4]; pos2 = 4
    n_in2, adv = _read_varint(unsigned_tx, pos2); pos2 += adv
    out = version + varint(n_in2)
    for i in range(n_in2):
        txid = unsigned_tx[pos2:pos2+36]; pos2 += 36
        sslen, adv = _read_varint(unsigned_tx, pos2); pos2 += adv
        pos2 += sslen
        seq = unsigned_tx[pos2:pos2+4]; pos2 += 4
        
        sig = sigs[i]
        scriptsig = bytes([len(sig)]) + sig + bytes([0x21]) + pubkey
        out += txid + varint(len(scriptsig)) + scriptsig + seq
        
    out += unsigned_tx[pos2:]
    return out

def random_tx(n_inputs, n_outputs):
    inputs = []
    for _ in range(n_inputs):
        txid = os.urandom(32)
        vout = random.randint(0, 3)
        value = random.randint(100000, 10_000_000_000)
        inputs.append((txid, vout, value))
        
    outputs = []
    for i in range(n_outputs):
        value = random.randint(100000, 5_000_000_000)
        if i == n_outputs - 1 and random.random() < 0.3:
            # OP_RETURN
            data = os.urandom(random.randint(1, 20))
            spk = bytes([0x6a, len(data)]) + data
        else:
            # P2PKH to a random key
            rpub = privkey_to_pubkey(os.urandom(32))
            spk = make_p2pkh_script(rpub)
        outputs.append((value, spk))
        
    return make_unsigned_tx(inputs, outputs)

# ---------------------------------------------------------------------------
# App Wrappers & Core Logic
# ---------------------------------------------------------------------------

def generate_random_mnemonic():
    """Generate a random 12-word BIP39 mnemonic"""
    mnemo = Mnemonic("english")
    entropy = os.urandom(16)  # 128 bits
    return mnemo.to_mnemonic(entropy)

def get_bip32_child(mnemonic):
    """Derive keys via Python (m/44'/3'/0'/0/0)"""
    mnemo = Mnemonic("english")
    seed = mnemo.to_seed(mnemonic, passphrase="")
    bip32_root = BIP32Key.fromEntropy(seed)
    return (bip32_root
            .ChildKey(44 + 0x80000000)
            .ChildKey(3 + 0x80000000)
            .ChildKey(0 + 0x80000000)
            .ChildKey(0)
            .ChildKey(0))

def python_mnemonic_to_address(bip32_child):
    """Compute Dogecoin address using Python"""
    pubkey = bip32_child.PublicKey()
    sha256_hash = hashlib.sha256(pubkey).digest()
    ripemd160 = hashlib.new('ripemd160')
    ripemd160.update(sha256_hash)
    hash160 = ripemd160.digest()
    
    versioned = b'\x1e' + hash160  # Dogecoin mainnet
    checksum = hashlib.sha256(hashlib.sha256(versioned).digest()).digest()[:4]
    address_bytes = versioned + checksum
    return base58.b58encode(address_bytes).decode('utf-8')

def c_mnemonic_to_address(mnemonic, binary):
    """Run C app to compute address"""
    try:
        result = subprocess.run([binary, mnemonic], capture_output=True, text=True, timeout=30)
        if result.returncode == 0: return result.stdout.strip()
        else:
            print(f"Address C app failed: {result.stderr}")
            return None
    except FileNotFoundError:
        print(f"C app {binary} not found - compile it first")
        return None
    except subprocess.TimeoutExpired:
        print("Address C app timed out")
        return None

def c_sign_tx(mnemonic, tx_hex, binary):
    """Run C app to sign transaction"""
    try:
        result = subprocess.run([binary, mnemonic, tx_hex], capture_output=True, text=True, timeout=30)
        if result.returncode == 0: return result.stdout.strip()
        else:
            print(f"Signing C app failed: {result.stderr.strip()}")
            return None
    except FileNotFoundError:
        print(f"C app {binary} not found - compile it first")
        return None
    except subprocess.TimeoutExpired:
        print("Signing C app timed out")
        return None

def main():
    parser = argparse.ArgumentParser(description="Validate Dogecoin crypto operations (Address + Signing)")
    parser.add_argument('--tests', type=int, default=20, help='Number of random tests to run')
    parser.add_argument('--addr-bin', type=str, default='./test/mnemonic_to_address', help='Path to address generator binary')
    parser.add_argument('--sign-bin', type=str, default='./test/sign_tx', help='Path to tx signer binary')
    args = parser.parse_args()

    num_tests = args.tests
    passed_addr = 0
    passed_sign = 0

    print(f"Running {num_tests} crypto validation tests (Address & Signing)...")
    print("=" * 60)

    for i in range(num_tests):
        mnemonic = generate_random_mnemonic()
        
        # Shared key derivation for both Python checks
        bip32_child = get_bip32_child(mnemonic)
        privkey = bip32_child.PrivateKey()
        pubkey = bip32_child.PublicKey()

        # -----------------------------------------------------------------
        # 1. Address Validation
        # -----------------------------------------------------------------
        expected_addr = python_mnemonic_to_address(bip32_child)
        actual_addr = c_mnemonic_to_address(mnemonic, args.addr_bin)
        
        addr_ok = (actual_addr == expected_addr)
        if addr_ok:
            passed_addr += 1

        # -----------------------------------------------------------------
        # 2. Signature Validation
        # -----------------------------------------------------------------
        n_in = random.randint(1, 4)
        n_out = random.randint(1, 4)
        unsigned_tx = random_tx(n_inputs=n_in, n_outputs=n_out)
        tx_hex = unsigned_tx.hex()

        # Python Reference Sign
        py_signed = sign_tx_python(unsigned_tx, privkey, pubkey)
        expected_tx_hex = py_signed.hex()

        # C App Sign
        actual_tx_hex = c_sign_tx(mnemonic, tx_hex, args.sign_bin)
        
        sign_ok = (actual_tx_hex == expected_tx_hex)
        if sign_ok:
            passed_sign += 1

        # -----------------------------------------------------------------
        # Output Logging
        # -----------------------------------------------------------------
        addr_status = "PASS" if addr_ok else "FAIL"
        sign_status = "PASS" if sign_ok else "FAIL"
        print(f"Test {i+1:3d} | Addr: {addr_status} | Sign: {sign_status} (In:{n_in}, Out:{n_out})")

        if not sign_ok:
            print(f"   Mnemonic: {mnemonic}")
            print(f"   Unsigned: {tx_hex[:40]}...")
            print(f"   Expected: {expected_tx_hex[:40]}...")
            print(f"   Actual  : {actual_tx_hex[:40] if actual_tx_hex else '(empty)'}...")

    # Summary
    print("=" * 60)
    print(f"Address Results : {passed_addr}/{num_tests} passed ({(passed_addr/num_tests)*100:.1f}%)")
    print(f"Signing Results : {passed_sign}/{num_tests} passed ({(passed_sign/num_tests)*100:.1f}%)")

    if passed_addr == num_tests and passed_sign == num_tests:
        print("\n✓ All tests passed! Crypto implementation is correct.")
        return 0
    else:
        print("\n✗ Some tests failed. Check implementation.")
        return 1

if __name__ == "__main__":
    sys.exit(main())