"""
Reliable chunked transport over the existing pulse-distance IR link.

Layering (highest -> lowest):
    application messages       (wire_format.py)
        |
    chunked transport         <-- THIS MODULE
        |  per-chunk CRC32 + ACK + retries
    Arduino USB-serial bridge  (arduino_bridge/arduino_bridge.ino)
        |  line-based command/response over USB
    raw IR bitbang             (existing pulse-distance encoding on Arduino)
        |
    GBC

----------------------------------------------------------------------------
Chunk format (over IR)
----------------------------------------------------------------------------

    +------+------+-----+-----+-------+-----+-----------+----------+
    | 0xDC | 0x6E | TYPE| SEQ | TOTAL | LEN |   DATA    | CRC32 BE |
    +------+------+-----+-----+-------+-----+-----------+----------+
       1     1      1     1      1      1     LEN bytes    4

Header is 6 bytes, CRC32 is 4 bytes.
CRC32 (IEEE 802.3, same as zlib.crc32) is computed over header + data,
serialized big-endian.

Types:
    0x01  DATA  — payload chunk, SEQ in 0..TOTAL-1
    0x02  ACK   — TOTAL=0, LEN=0; SEQ = chunk being acked
    0x03  NACK  — same shape as ACK; SEQ = chunk that failed
    0x04  PING  — TOTAL=0, LEN=0
    0x05  PONG  — TOTAL=0, LEN=0

Max DATA payload per chunk: 192 bytes (so max chunk wire size = 202 bytes,
which keeps a single IR transmission under ~6 seconds at our bit rate).

----------------------------------------------------------------------------
PC <-> Arduino protocol (USB serial, 115200 baud, line-based)
----------------------------------------------------------------------------

PC -> Arduino:
    PNG\n                          ping
    TXR <ms> <hex>\n               transmit `hex` bytes over IR, then listen
                                   for one chunk with up to `ms` ms timeout
    RX <ms>\n                      listen-only for up to `ms` ms

Arduino -> PC (responses always start with '<'):
    <PONG\n
    <RXOK <hex>\n                  got bytes from IR
    <TIMEOUT\n                     no IR data within timeout
    <ERR <reason>\n                e.g. malformed command, bus stuck
    # <text>\n                     informational debug (PC may ignore)

The Arduino does NOT do any framing or CRC — it just shuttles bytes between
USB and IR. All reliability lives in this Python module so the same protocol
can be reused if we ever swap the Arduino for something else.
"""

import logging
import struct
import threading
import time
import zlib
from dataclasses import dataclass
from queue import Empty, Queue
from typing import Callable, Optional

log = logging.getLogger("ir_transport")

# --- Chunk constants ------------------------------------------------------

MAGIC_0 = 0xDC
MAGIC_1 = 0x6E

CHUNK_DATA = 0x01
CHUNK_ACK = 0x02
CHUNK_NACK = 0x03
CHUNK_PING = 0x04
CHUNK_PONG = 0x05

CHUNK_HEADER_SIZE = 6
CHUNK_CRC_SIZE = 4
MAX_CHUNK_DATA = 192

# Timing
DEFAULT_ACK_TIMEOUT_MS = 1500       # how long to wait for an ACK after a DATA chunk
DEFAULT_RX_TIMEOUT_MS = 30_000      # how long to listen when expecting incoming
MAX_CHUNK_RETRIES = 4
INTER_CHUNK_BACKOFF_S = 0.10        # short pause between retries


# --- CRC + chunk codec ----------------------------------------------------

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


@dataclass
class Chunk:
    type: int
    seq: int
    total: int
    data: bytes


def encode_chunk(c: Chunk) -> bytes:
    if len(c.data) > MAX_CHUNK_DATA:
        raise ValueError(f"data too large for chunk: {len(c.data)}")
    if not (0 <= c.seq <= 255 and 0 <= c.total <= 255):
        raise ValueError("seq/total out of range")
    header = bytes([MAGIC_0, MAGIC_1, c.type, c.seq, c.total, len(c.data)])
    body = header + c.data
    return body + struct.pack(">I", crc32(body))


class FramingError(Exception):
    pass


def decode_chunk(raw: bytes) -> Chunk:
    if len(raw) < CHUNK_HEADER_SIZE + CHUNK_CRC_SIZE:
        raise FramingError(f"chunk too short: {len(raw)} bytes")
    if raw[0] != MAGIC_0 or raw[1] != MAGIC_1:
        raise FramingError(f"bad magic: {raw[0]:02x} {raw[1]:02x}")
    type_, seq, total, length = raw[2], raw[3], raw[4], raw[5]
    expected_total = CHUNK_HEADER_SIZE + length + CHUNK_CRC_SIZE
    if len(raw) != expected_total:
        raise FramingError(
            f"length mismatch: header says {length} data bytes "
            f"(expect {expected_total} total), got {len(raw)}"
        )
    body = raw[: CHUNK_HEADER_SIZE + length]
    got_crc = struct.unpack(">I", raw[-4:])[0]
    want_crc = crc32(body)
    if got_crc != want_crc:
        raise FramingError(
            f"crc mismatch: got 0x{got_crc:08x}, want 0x{want_crc:08x}"
        )
    return Chunk(type=type_, seq=seq, total=total, data=bytes(raw[6 : 6 + length]))


# --- Arduino bridge -------------------------------------------------------

class TransportError(Exception):
    pass


class ArduinoBridge:
    """
    Talks to the Arduino over USB serial.

    Each method blocks until it gets a response (or timeout).
    """

    def __init__(self, port: str, baud: int = 115200, debug_log: Optional[Callable[[str], None]] = None):
        import serial  # imported lazily so dogecoin.py / wire_format.py work without pyserial

        self._serial = serial.Serial(port, baud, timeout=0.05)
        # On most boards opening the serial port resets the Arduino. Give it a
        # moment to come up before we send anything.
        time.sleep(2.0)
        try:
            self._serial.reset_input_buffer()
        except Exception:
            pass
        self._debug_log = debug_log
        self._rx_lines: Queue = Queue()
        self._stop = threading.Event()
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader.start()

    def _reader_loop(self):
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self._serial.read(256)
            except Exception as e:
                log.warning("serial read error: %s", e)
                time.sleep(0.1)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                try:
                    decoded = line.decode("ascii", errors="replace").rstrip("\r")
                except Exception:
                    decoded = ""
                if decoded.startswith("# "):
                    if self._debug_log:
                        self._debug_log(decoded[2:])
                else:
                    self._rx_lines.put(decoded)

    def _read_response(self, timeout_s: float) -> str:
        try:
            line = self._rx_lines.get(timeout=timeout_s)
        except Empty:
            raise TransportError(f"no response from Arduino within {timeout_s:.1f}s")
        return line

    def _send_command(self, cmd: str):
        if not cmd.endswith("\n"):
            cmd = cmd + "\n"
        self._serial.write(cmd.encode("ascii"))
        self._serial.flush()

    def ping(self, timeout_s: float = 2.0) -> bool:
        # Drain any pending lines first
        while not self._rx_lines.empty():
            try:
                self._rx_lines.get_nowait()
            except Empty:
                break
        self._send_command("PNG")
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                line = self._read_response(deadline - time.monotonic())
            except TransportError:
                return False
            if line == "<PONG":
                return True
        return False

    def transmit_and_listen(
        self, payload: bytes, listen_ms: int
    ) -> Optional[bytes]:
        """
        Transmits `payload` over IR, then listens for an incoming chunk for up
        to `listen_ms` ms. Returns the received bytes, or None on timeout.
        """
        cmd = f"TXR {listen_ms} {payload.hex()}"
        self._send_command(cmd)
        # Need extra time over `listen_ms` for the actual TX to finish
        # (~30ms per byte at IR rates) plus serial overhead.
        budget_s = (listen_ms + len(payload) * 60 + 1000) / 1000.0
        deadline = time.monotonic() + budget_s
        while time.monotonic() < deadline:
            line = self._read_response(deadline - time.monotonic())
            if line.startswith("<RXOK "):
                return bytes.fromhex(line[6:].strip())
            if line == "<TIMEOUT":
                return None
            if line.startswith("<ERR"):
                raise TransportError(f"arduino error: {line}")
            # Ignore anything else (could be late <PONG, etc.)
        raise TransportError("timed out waiting for TXR response")

    def listen(self, timeout_ms: int) -> Optional[bytes]:
        cmd = f"RX {timeout_ms}"
        self._send_command(cmd)
        budget_s = (timeout_ms + 2000) / 1000.0
        deadline = time.monotonic() + budget_s
        while time.monotonic() < deadline:
            line = self._read_response(deadline - time.monotonic())
            if line.startswith("<RXOK "):
                return bytes.fromhex(line[6:].strip())
            if line == "<TIMEOUT":
                return None
            if line.startswith("<ERR"):
                raise TransportError(f"arduino error: {line}")
        raise TransportError("timed out waiting for RX response")

    def close(self):
        self._stop.set()
        try:
            self._serial.close()
        except Exception:
            pass


# --- High-level chunked transport -----------------------------------------

class ChunkedTransport:
    """
    Reliable message transport. Splits messages into chunks, sends each with
    ACK/retry, reassembles incoming.
    """

    def __init__(self, bridge: ArduinoBridge, chunk_data_size: int = 64):
        if not 1 <= chunk_data_size <= MAX_CHUNK_DATA:
            raise ValueError(f"chunk_data_size must be 1..{MAX_CHUNK_DATA}")
        self.bridge = bridge
        self.chunk_size = chunk_data_size

    # -- Send -----------------------------------------------------------

    def send_message(
        self,
        payload: bytes,
        progress: Optional[Callable[[int, int], None]] = None,
    ):
        """
        Sends `payload` as a sequence of DATA chunks. After each chunk, waits
        for an ACK with matching seq. Retries on NACK / corruption / timeout.

        Raises TransportError on persistent failure.
        """
        if len(payload) == 0:
            raise ValueError("won't send empty payload")
        n_chunks = (len(payload) + self.chunk_size - 1) // self.chunk_size
        if n_chunks > 255:
            raise ValueError(
                f"message needs {n_chunks} chunks; max 255 — increase chunk_size"
            )

        if progress:
            progress(0, n_chunks)

        for seq in range(n_chunks):
            data = payload[seq * self.chunk_size : (seq + 1) * self.chunk_size]
            chunk = encode_chunk(Chunk(CHUNK_DATA, seq, n_chunks, data))

            for attempt in range(MAX_CHUNK_RETRIES):
                log.debug(
                    "TX chunk %d/%d (attempt %d, %d wire bytes)",
                    seq + 1, n_chunks, attempt + 1, len(chunk),
                )
                resp = self.bridge.transmit_and_listen(
                    chunk, listen_ms=DEFAULT_ACK_TIMEOUT_MS
                )
                if resp is None:
                    log.warning("no response for chunk %d (attempt %d)", seq, attempt + 1)
                    time.sleep(INTER_CHUNK_BACKOFF_S)
                    continue
                try:
                    ack = decode_chunk(resp)
                except FramingError as e:
                    log.warning("framing error in ACK: %s", e)
                    time.sleep(INTER_CHUNK_BACKOFF_S)
                    continue
                if ack.type == CHUNK_ACK and ack.seq == seq:
                    break
                if ack.type == CHUNK_NACK and ack.seq == seq:
                    log.info("got NACK for chunk %d, retrying", seq)
                    time.sleep(INTER_CHUNK_BACKOFF_S)
                    continue
                log.warning(
                    "unexpected response: type=0x%02x seq=%d (wanted ACK seq=%d)",
                    ack.type, ack.seq, seq,
                )
                time.sleep(INTER_CHUNK_BACKOFF_S)
            else:
                raise TransportError(
                    f"chunk {seq + 1}/{n_chunks} failed after {MAX_CHUNK_RETRIES} retries"
                )

            if progress:
                progress(seq + 1, n_chunks)

    # -- Receive --------------------------------------------------------

    def receive_message(self, first_chunk_timeout_ms: int = DEFAULT_RX_TIMEOUT_MS) -> bytes:
        """
        Listens for incoming chunks, ACKs each, reassembles, and returns the
        full payload. Raises TransportError on timeout or persistent corruption.
        """
        chunks: dict = {}
        total: Optional[int] = None
        timeout_ms = first_chunk_timeout_ms

        while True:
            raw = self.bridge.listen(timeout_ms)
            if raw is None:
                if total is None:
                    raise TransportError("timed out waiting for first chunk")
                raise TransportError(
                    f"timed out waiting for chunk; have {len(chunks)}/{total}"
                )

            try:
                ch = decode_chunk(raw)
            except FramingError as e:
                # Best-effort NACK: we don't know seq, so send NACK seq=0
                log.warning("framing error: %s — sending NACK", e)
                nack = encode_chunk(Chunk(CHUNK_NACK, 0, 0, b""))
                self.bridge.transmit_and_listen(nack, listen_ms=200)
                timeout_ms = DEFAULT_RX_TIMEOUT_MS
                continue

            if ch.type != CHUNK_DATA:
                log.warning("ignoring non-DATA chunk type 0x%02x", ch.type)
                timeout_ms = DEFAULT_RX_TIMEOUT_MS
                continue

            if total is None:
                total = ch.total
                if total == 0 or total > 255:
                    raise TransportError(f"invalid total in first chunk: {total}")
            elif ch.total != total:
                log.warning(
                    "chunk reports total=%d but first chunk said %d", ch.total, total
                )

            chunks[ch.seq] = ch.data
            # Send ACK back over IR (and immediately listen for the next data chunk)
            ack = encode_chunk(Chunk(CHUNK_ACK, ch.seq, 0, b""))
            # Short listen window after ACK so we can immediately catch the next data chunk
            self.bridge.transmit_and_listen(ack, listen_ms=200)

            if len(chunks) == total:
                break

            timeout_ms = DEFAULT_RX_TIMEOUT_MS

        # Reassemble in order
        try:
            return b"".join(chunks[i] for i in range(total))
        except KeyError as e:
            raise TransportError(f"missing chunk {e} after assembly")


# --- Self-test -------------------------------------------------------------

if __name__ == "__main__":
    # Round-trip a chunk through encode/decode
    msg = b"the quick brown doge jumps over the moon" * 3
    c = encode_chunk(Chunk(CHUNK_DATA, 5, 17, msg))
    print(f"encoded chunk: {len(c)} bytes ({len(msg)} payload)")
    back = decode_chunk(c)
    assert back.data == msg and back.seq == 5 and back.total == 17
    print("chunk round-trip OK")

    # Tampered byte should fail CRC
    bad = bytearray(c)
    bad[10] ^= 0x01
    try:
        decode_chunk(bytes(bad))
        print("UNEXPECTED: tampered chunk passed CRC")
    except FramingError as e:
        print(f"tampered chunk correctly rejected: {e}")
