"""
Application-layer wire format for DogeGB <-> companion communication.

This sits ON TOP of the chunked CRC32 transport (see ir_transport.py).

Message envelope (carried in the data of a chunked message):

    +------+------+------+--------------+
    | TYPE | LEN_HI | LEN_LO | PAYLOAD ...
    +------+--------+--------+----------+
    1 byte  2 bytes BE         LEN bytes

Message types:
    0x01  TX_PROPOSAL  (companion -> GBC)
    0x02  SIGNED_TX    (GBC -> companion)
    0x03  PING         (either)
    0x04  PONG         (either)
    0x05  ERROR        (either)  payload = ASCII reason

----------------------------------------------------------------------------
TX_PROPOSAL payload
----------------------------------------------------------------------------
Custom binary format. Keeps things small enough for IR and trivially parseable
on the GBC. NOT a standard Bitcoin transaction — the GBC reconstructs the
canonical tx from these fields, signs it, and sends back a fully-signed
SIGNED_TX (which IS a standard Bitcoin tx hex).

    [1]   wire format version          (= 0x01)
    [4 BE] tx version                   (Bitcoin tx version, normally 1)
    [4 BE] locktime                     (normally 0)

    [1]   number of inputs N           (must be >= 1, max 8)
    For each of N inputs:
        [32]   prev txid               (BIG-ENDIAN display order — same as
                                        block explorers; GBC must reverse it
                                        when serializing the tx)
        [4 BE] prev vout
        [8 BE] value in koinu
        [1]    prev scriptPubKey length
        [...]  prev scriptPubKey

    [1]   number of outputs M          (1..4)
    For each of M outputs:
        [8 BE] value in koinu
        [1]    scriptPubKey length
        [...]  scriptPubKey

    [4 BE] sighash type                (= 0x00000001 for SIGHASH_ALL)

All multi-byte integers are BIG-ENDIAN to make GBC parsing trivial (the GBC
just reads bytes off the wire MSB-first into uint32s/uint64s).

----------------------------------------------------------------------------
SIGNED_TX payload
----------------------------------------------------------------------------
Just the raw signed transaction bytes (Bitcoin standard serialization). The
companion app broadcasts this to the network unchanged.
"""

import struct
from dataclasses import dataclass
from typing import List

from dogecoin import TxInput, TxOutput

# --- Message types ---------------------------------------------------------

MSG_TX_PROPOSAL = 0x01
MSG_SIGNED_TX = 0x02
MSG_PING = 0x03
MSG_PONG = 0x04
MSG_ERROR = 0x05

WIRE_FORMAT_VERSION = 0x01
SIGHASH_ALL = 0x00000001

MAX_MESSAGE_PAYLOAD = 65535  # 16-bit length


# --- Envelope --------------------------------------------------------------

def pack_message(msg_type: int, payload: bytes) -> bytes:
    if not (0 < msg_type <= 0xFF):
        raise ValueError("msg_type must be 1..255")
    if len(payload) > MAX_MESSAGE_PAYLOAD:
        raise ValueError(
            f"payload too large: {len(payload)} > {MAX_MESSAGE_PAYLOAD}"
        )
    return bytes([msg_type]) + struct.pack(">H", len(payload)) + payload


def unpack_message(data: bytes) -> tuple:
    """Returns (msg_type, payload). Raises ValueError on malformed input."""
    if len(data) < 3:
        raise ValueError("message too short")
    msg_type = data[0]
    (length,) = struct.unpack(">H", data[1:3])
    if len(data) < 3 + length:
        raise ValueError(
            f"truncated message: header says {length} bytes, got {len(data) - 3}"
        )
    return msg_type, data[3 : 3 + length]


# --- TX_PROPOSAL pack ------------------------------------------------------

def pack_tx_proposal(
    inputs: List[TxInput],
    outputs: List[TxOutput],
    tx_version: int = 1,
    locktime: int = 0,
    sighash_type: int = SIGHASH_ALL,
) -> bytes:
    if not 1 <= len(inputs) <= 8:
        raise ValueError(f"inputs out of range: {len(inputs)} (must be 1..8)")
    if not 1 <= len(outputs) <= 4:
        raise ValueError(f"outputs out of range: {len(outputs)} (must be 1..4)")

    out = bytearray()
    out.append(WIRE_FORMAT_VERSION)
    out += struct.pack(">I", tx_version)
    out += struct.pack(">I", locktime)

    out.append(len(inputs))
    for inp in inputs:
        txid_bytes = bytes.fromhex(inp.prev_txid)
        if len(txid_bytes) != 32:
            raise ValueError(f"bad txid length: {len(txid_bytes)}")
        out += txid_bytes  # BIG-endian display order
        out += struct.pack(">I", inp.prev_vout)
        out += struct.pack(">Q", inp.value)
        if len(inp.prev_script_pubkey) > 255:
            raise ValueError("scriptPubKey too long for 1-byte length")
        out.append(len(inp.prev_script_pubkey))
        out += inp.prev_script_pubkey

    out.append(len(outputs))
    for o in outputs:
        out += struct.pack(">Q", o.value)
        if len(o.script_pubkey) > 255:
            raise ValueError("scriptPubKey too long for 1-byte length")
        out.append(len(o.script_pubkey))
        out += o.script_pubkey

    out += struct.pack(">I", sighash_type)
    return bytes(out)


# --- TX_PROPOSAL parse (mostly for tests / Python-side review) -------------

@dataclass
class ParsedProposal:
    wire_version: int
    tx_version: int
    locktime: int
    inputs: List[TxInput]
    outputs: List[TxOutput]
    sighash_type: int


def parse_tx_proposal(data: bytes) -> ParsedProposal:
    p = 0

    def read(n):
        nonlocal p
        if p + n > len(data):
            raise ValueError(f"truncated proposal at offset {p}")
        b = data[p : p + n]
        p += n
        return b

    wire_version = read(1)[0]
    if wire_version != WIRE_FORMAT_VERSION:
        raise ValueError(f"unknown wire format version 0x{wire_version:02x}")

    (tx_version,) = struct.unpack(">I", read(4))
    (locktime,) = struct.unpack(">I", read(4))

    n_in = read(1)[0]
    inputs = []
    for _ in range(n_in):
        txid = read(32).hex()
        (vout,) = struct.unpack(">I", read(4))
        (value,) = struct.unpack(">Q", read(8))
        spk_len = read(1)[0]
        spk = bytes(read(spk_len))
        inputs.append(TxInput(txid, vout, value, spk))

    n_out = read(1)[0]
    outputs = []
    for _ in range(n_out):
        (value,) = struct.unpack(">Q", read(8))
        spk_len = read(1)[0]
        spk = bytes(read(spk_len))
        outputs.append(TxOutput(value, spk))

    (sighash_type,) = struct.unpack(">I", read(4))

    if p != len(data):
        raise ValueError(f"trailing bytes after proposal ({len(data) - p} extra)")

    return ParsedProposal(
        wire_version, tx_version, locktime, inputs, outputs, sighash_type
    )


# --- Self-test --------------------------------------------------------------

if __name__ == "__main__":
    from dogecoin import script_pubkey_p2pkh, script_op_return

    inputs = [
        TxInput("aa" * 32, 0, 1_000_000_000, script_pubkey_p2pkh(b"\x11" * 20)),
        TxInput("bb" * 32, 1, 500_000_000, script_pubkey_p2pkh(b"\x22" * 20)),
    ]
    outputs = [
        TxOutput(1_200_000_000, script_pubkey_p2pkh(b"\x33" * 20)),
        TxOutput(0, script_op_return(b"hello DogeGB")),
    ]
    raw = pack_tx_proposal(inputs, outputs)
    print(f"proposal size: {len(raw)} bytes")
    print(f"hex: {raw.hex()}")

    parsed = parse_tx_proposal(raw)
    print(f"parsed back: {len(parsed.inputs)} inputs, {len(parsed.outputs)} outputs")
    assert parsed.inputs[0].prev_txid == "aa" * 32
    assert parsed.inputs[0].value == 1_000_000_000
    assert parsed.outputs[0].value == 1_200_000_000
    print("round-trip OK")

    env = pack_message(MSG_TX_PROPOSAL, raw)
    mt, payload = unpack_message(env)
    assert mt == MSG_TX_PROPOSAL and payload == raw
    print("envelope round-trip OK")
