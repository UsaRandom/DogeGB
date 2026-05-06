"""
Dogecoin transaction primitives (no signing — pure stdlib).

All this module does is decode addresses, build scriptPubKeys, and serialize
unsigned transactions. The GBC handles signing.

Doge address prefixes:
    P2PKH  0x1E  (decimal 30)  — addresses starting with 'D'
    P2SH   0x16  (decimal 22)  — addresses starting with 'A' or '9'

Units:
    1 DOGE = 100_000_000 koinu (smallest unit, like satoshis)
"""

import hashlib
import struct
from dataclasses import dataclass
from typing import List, Optional

KOINU_PER_DOGE = 100_000_000

DOGE_P2PKH_PREFIX = 0x1E
DOGE_P2SH_PREFIX = 0x16

# Standard Bitcoin opcodes we need
OP_DUP = 0x76
OP_HASH160 = 0xA9
OP_EQUAL = 0x87
OP_EQUALVERIFY = 0x88
OP_CHECKSIG = 0xAC
OP_RETURN = 0x6A
OP_PUSHDATA1 = 0x4C

OP_RETURN_MAX_LEN = 80  # Standard Bitcoin/Doge relay policy


# --- Hashing ---------------------------------------------------------------

def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def sha256d(data: bytes) -> bytes:
    """Bitcoin's standard double-SHA256."""
    return sha256(sha256(data))


# --- Base58Check -----------------------------------------------------------

_B58 = b"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def b58_decode(s: str) -> bytes:
    raw = s.encode() if isinstance(s, str) else s
    n = 0
    for c in raw:
        idx = _B58.find(c)
        if idx < 0:
            raise ValueError(f"Invalid base58 character: {chr(c)!r}")
        n = n * 58 + idx
    body = n.to_bytes((n.bit_length() + 7) // 8, "big") if n else b""
    leading = 0
    for c in raw:
        if c == ord("1"):
            leading += 1
        else:
            break
    return b"\x00" * leading + body


def b58_encode(data: bytes) -> str:
    n = int.from_bytes(data, "big") if data else 0
    out = bytearray()
    while n > 0:
        n, r = divmod(n, 58)
        out.append(_B58[r])
    for c in data:
        if c == 0:
            out.append(_B58[0])
        else:
            break
    return out[::-1].decode()


def b58check_decode(s: str) -> bytes:
    """Returns the decoded payload (1-byte version + body) without checksum."""
    raw = b58_decode(s)
    if len(raw) < 5:
        raise ValueError("Address too short")
    body, checksum = raw[:-4], raw[-4:]
    if sha256d(body)[:4] != checksum:
        raise ValueError("Bad base58check checksum")
    return body


def b58check_encode(payload: bytes) -> str:
    """Adds a 4-byte SHA256d checksum and base58-encodes."""
    return b58_encode(payload + sha256d(payload)[:4])


# --- Address handling ------------------------------------------------------

@dataclass
class DecodedAddress:
    address_type: str  # 'p2pkh' or 'p2sh'
    hash160: bytes
    original: str

    def script_pubkey(self) -> bytes:
        if self.address_type == "p2pkh":
            return script_pubkey_p2pkh(self.hash160)
        return script_pubkey_p2sh(self.hash160)


def decode_address(s: str) -> DecodedAddress:
    s = s.strip()
    payload = b58check_decode(s)
    if len(payload) != 21:
        raise ValueError(f"Unexpected payload length: {len(payload)}")
    version, h160 = payload[0], payload[1:]
    if version == DOGE_P2PKH_PREFIX:
        return DecodedAddress("p2pkh", h160, s)
    if version == DOGE_P2SH_PREFIX:
        return DecodedAddress("p2sh", h160, s)
    raise ValueError(
        f"Not a Dogecoin P2PKH or P2SH address (version byte 0x{version:02x})"
    )


def is_valid_doge_address(s: str) -> bool:
    try:
        decode_address(s)
        return True
    except Exception:
        return False


# --- ScriptPubKey builders -------------------------------------------------

def script_pubkey_p2pkh(h160: bytes) -> bytes:
    if len(h160) != 20:
        raise ValueError("hash160 must be 20 bytes")
    return bytes([OP_DUP, OP_HASH160, 20]) + h160 + bytes([OP_EQUALVERIFY, OP_CHECKSIG])


def script_pubkey_p2sh(h160: bytes) -> bytes:
    if len(h160) != 20:
        raise ValueError("hash160 must be 20 bytes")
    return bytes([OP_HASH160, 20]) + h160 + bytes([OP_EQUAL])


def script_op_return(data: bytes) -> bytes:
    if len(data) > OP_RETURN_MAX_LEN:
        raise ValueError(
            f"OP_RETURN data exceeds {OP_RETURN_MAX_LEN}-byte standard relay policy"
        )
    if len(data) <= 75:
        # Direct push: opcode == length
        return bytes([OP_RETURN, len(data)]) + data
    # 76..80: OP_PUSHDATA1
    return bytes([OP_RETURN, OP_PUSHDATA1, len(data)]) + data


# --- Varint / serialization ------------------------------------------------

def varint(n: int) -> bytes:
    if n < 0:
        raise ValueError("varint must be non-negative")
    if n < 0xFD:
        return bytes([n])
    if n <= 0xFFFF:
        return b"\xfd" + n.to_bytes(2, "little")
    if n <= 0xFFFFFFFF:
        return b"\xfe" + n.to_bytes(4, "little")
    return b"\xff" + n.to_bytes(8, "little")


# --- Transaction structures ------------------------------------------------

@dataclass
class TxInput:
    prev_txid: str         # hex, big-endian display order (as shown in explorers)
    prev_vout: int
    value: int             # koinu, for display + sighash on segwit (unused for legacy but useful)
    prev_script_pubkey: bytes
    sequence: int = 0xFFFFFFFF


@dataclass
class TxOutput:
    value: int             # koinu
    script_pubkey: bytes


def serialize_unsigned_tx(
    inputs: List[TxInput],
    outputs: List[TxOutput],
    version: int = 1,
    locktime: int = 0,
    embed_prev_scripts: bool = True,
) -> bytes:
    """
    Serialize an unsigned transaction in standard Bitcoin format.

    If `embed_prev_scripts` is True, each input's scriptSig field is set to the
    prev output's scriptPubKey. This is the standard "ready-to-sign" format for
    legacy (non-segwit) signing — the signer can compute the per-input sighash
    by zeroing the other inputs' scriptSigs, hashing, and signing.

    If False, scriptSigs are left empty (just a length-0 varint).
    """
    out = bytearray()
    out += struct.pack("<I", version)

    out += varint(len(inputs))
    for inp in inputs:
        # Wire format uses txid in *reversed* (little-endian) byte order
        out += bytes.fromhex(inp.prev_txid)[::-1]
        out += struct.pack("<I", inp.prev_vout)
        if embed_prev_scripts:
            out += varint(len(inp.prev_script_pubkey)) + inp.prev_script_pubkey
        else:
            out += varint(0)
        out += struct.pack("<I", inp.sequence)

    out += varint(len(outputs))
    for o in outputs:
        out += struct.pack("<Q", o.value)
        out += varint(len(o.script_pubkey)) + o.script_pubkey

    out += struct.pack("<I", locktime)
    return bytes(out)


# --- Coin selection / fee estimation ---------------------------------------

# Conservative legacy P2PKH transaction sizing (bytes per element, signed):
#   per-input  ≈ 148  (32 txid + 4 vout + 1 scriptlen + 107 scriptSig + 4 seq)
#   per-p2pkh-output ≈ 34  (8 value + 1 scriptlen + 25 script)
#   per-p2sh-output  ≈ 32
#   overhead   ≈ 10  (4 version + 1 in_count + 1 out_count + 4 locktime)

LEGACY_INPUT_BYTES = 148
LEGACY_P2PKH_OUTPUT_BYTES = 34
LEGACY_P2SH_OUTPUT_BYTES = 32
TX_OVERHEAD_BYTES = 10


def estimate_signed_tx_size(
    num_inputs: int,
    num_p2pkh_outputs: int,
    num_p2sh_outputs: int = 0,
    op_return_len: int = 0,
) -> int:
    size = (
        TX_OVERHEAD_BYTES
        + num_inputs * LEGACY_INPUT_BYTES
        + num_p2pkh_outputs * LEGACY_P2PKH_OUTPUT_BYTES
        + num_p2sh_outputs * LEGACY_P2SH_OUTPUT_BYTES
    )
    if op_return_len > 0:
        # 8 value + 1 scriptlen + 1 OP_RETURN + (1 or 2) push opcode + data
        push_overhead = 2 if op_return_len <= 75 else 3
        size += 8 + 1 + 1 + push_overhead + op_return_len
    return size


@dataclass
class CoinSelection:
    chosen: List[dict]   # subset of utxos
    total_in: int        # koinu
    total_out: int       # koinu (recipient + op_return + change)
    fee: int             # koinu
    change: int          # koinu (0 if no change output needed)


class InsufficientFundsError(Exception):
    pass


def select_coins_greedy(
    utxos: List[dict],
    send_value: int,
    fee_rate_koinu_per_byte: int,
    has_op_return: bool = False,
    op_return_len: int = 0,
    recipient_is_p2sh: bool = False,
    dust_threshold: int = 1_000_000,  # 0.01 DOGE — well above relay dust
) -> CoinSelection:
    """
    Pick UTXOs (largest first) until we cover send_value + estimated fee.
    Returns a change amount; if change < dust, the leftover is added to the
    fee instead of being a tiny dust output.

    `utxos` items must have at least 'value' (int koinu).
    """
    if send_value <= 0:
        raise ValueError("send_value must be positive")

    # Sort largest first
    sorted_utxos = sorted(utxos, key=lambda u: u["value"], reverse=True)

    chosen = []
    total_in = 0
    n_recipient_p2sh = 1 if recipient_is_p2sh else 0
    n_recipient_p2pkh = 0 if recipient_is_p2sh else 1

    for u in sorted_utxos:
        chosen.append(u)
        total_in += u["value"]

        # Try without change first
        size_no_change = estimate_signed_tx_size(
            num_inputs=len(chosen),
            num_p2pkh_outputs=n_recipient_p2pkh,
            num_p2sh_outputs=n_recipient_p2sh,
            op_return_len=op_return_len if has_op_return else 0,
        )
        fee_no_change = size_no_change * fee_rate_koinu_per_byte
        if total_in == send_value + fee_no_change:
            return CoinSelection(chosen, total_in, send_value, fee_no_change, 0)

        # With change (always P2PKH back to source)
        size_with_change = estimate_signed_tx_size(
            num_inputs=len(chosen),
            num_p2pkh_outputs=n_recipient_p2pkh + 1,
            num_p2sh_outputs=n_recipient_p2sh,
            op_return_len=op_return_len if has_op_return else 0,
        )
        fee_with_change = size_with_change * fee_rate_koinu_per_byte
        change = total_in - send_value - fee_with_change

        if change >= dust_threshold:
            return CoinSelection(
                chosen, total_in, send_value + change, fee_with_change, change
            )
        if total_in >= send_value + fee_no_change and change < dust_threshold:
            # Skip the change output; the would-be-change becomes extra fee
            return CoinSelection(
                chosen, total_in, send_value, total_in - send_value, 0
            )

    raise InsufficientFundsError(
        f"Need at least {send_value + fee_no_change} koinu but only "
        f"{total_in} available"
    )


def doge_to_koinu(doge: float) -> int:
    """Convert DOGE float to integer koinu, rounded to nearest."""
    return int(round(doge * KOINU_PER_DOGE))


def koinu_to_doge(koinu: int) -> float:
    return koinu / KOINU_PER_DOGE


def format_doge(koinu: int) -> str:
    """Format koinu as a DOGE string with 8 decimals, trimmed."""
    s = f"{koinu / KOINU_PER_DOGE:.8f}"
    # Trim trailing zeros but keep at least one decimal
    s = s.rstrip("0").rstrip(".") if "." in s else s
    return s


# --- Self-test --------------------------------------------------------------

if __name__ == "__main__":
    # Known-valid Dogecoin address (Dogecoin Foundation donation address from
    # block explorer references) — should decode to P2PKH with version 0x1E.
    test_addrs = [
        ("DH5yaieqoZN72WTV4WejXPpojfLk6wykUM", "p2pkh"),
        ("D8H1MiPjUk7vKkn6e6yLpb8oP9Pre9b2qN", "p2pkh"),
        ("A9fGSJpgZBLDWYP3yWVwKpD8gXn7Lcwu7Z", "p2sh"),
    ]
    for addr, expected in test_addrs:
        try:
            d = decode_address(addr)
            ok = d.address_type == expected
            print(
                f"  [{'OK' if ok else 'FAIL'}] {addr[:14]}... -> "
                f"{d.address_type}, h160={d.hash160.hex()}"
            )
        except Exception as e:
            print(f"  [ERR ] {addr[:14]}... -> {e}")

    # Round-trip check
    h160 = bytes(range(20))
    addr = b58check_encode(bytes([DOGE_P2PKH_PREFIX]) + h160)
    print(f"  Encoded sample: {addr}")
    print(f"  Decoded back:   {decode_address(addr).hash160.hex()}")

    # Tx serialization sanity check
    inp = TxInput(
        prev_txid="00" * 32,
        prev_vout=0,
        value=100_000_000,
        prev_script_pubkey=script_pubkey_p2pkh(b"\x00" * 20),
    )
    out = TxOutput(50_000_000, script_pubkey_p2pkh(b"\x11" * 20))
    raw = serialize_unsigned_tx([inp], [out])
    print(f"  Sample unsigned tx ({len(raw)} bytes): {raw.hex()}")

    # OP_RETURN
    op = script_op_return(b"hello dogegb")
    print(f"  OP_RETURN script: {op.hex()}")
