# DogeGB Companion App

A Python companion app for [DogeGB](https://github.com/usarandom/dogegb) — the
air-gapped Game Boy Color cold-storage wallet for Dogecoin.

The GBC holds the keys and signs transactions. This companion app does the
parts the GBC can't easily do:

- watch-only UTXO lookups against the live network (no API key required),
- transaction building (P2PKH / P2SH outputs, optional `OP_RETURN` up to
  80 bytes),
- transport to and from the GBC over IR via an Arduino bridge,
- broadcast of the signed transaction.

The companion **never sees your seed or private keys**. The Arduino is a
dumb byte-pipe — all crypto happens on the GBC.

---

## Round-trip flow

```
   PC (this app) ── USB ──► Arduino ── IR ──► GBC
                                                │
                                          (signs tx)
                                                │
   PC ◄─── USB ─── Arduino ◄─── IR ──────────── GBC
   │
   └──► broadcast to network
```

1. You enter your Doge address in the app, click **Fetch UTXOs**.
2. You enter recipient + amount (+ optional `OP_RETURN` message), click
   **Build transaction**.
3. The app encodes a `TX_PROPOSAL` (custom binary format, see below),
   wraps it in chunks with CRC32 framing, and sends it to the GBC over IR.
4. The GBC displays the destination + amount + fee for you to confirm,
   signs each input, and sends back a `SIGNED_TX`.
5. You click **Broadcast** and the app pushes the signed hex to the
   network.

---

## Quick start

### Install

```bash
pip install -r requirements.txt
```

### Flash the Arduino

Open `arduino_bridge/arduino_bridge.ino` in the Arduino IDE.

Wiring (matches the reference sketch):

| Arduino    | Component        |
| ---------- | ---------------- |
| D2         | IR LED anode (with current-limiting resistor to GND) |
| A6         | IR phototransistor / sensor input |
| 5V / GND   | sensor power     |

Flash to any AVR-based Arduino (Uno / Nano / Pro Mini / Mega).

### Run the app

```bash
python3 app.py
```

The window opens with three sections: Wallet, New Transaction, IR Transport.

---

## File layout

```
dogegb-companion/
├── app.py                       # CustomTkinter UI
├── dogecoin.py                  # Doge primitives (base58check, scripts, tx serialization)
├── wire_format.py               # Application messages (TX_PROPOSAL, SIGNED_TX, ...)
├── ir_transport.py              # Chunked CRC32 transport, ACK/retry, Arduino bridge
├── utxo_api.py                  # BlockCypher + chainz.cryptoid.info clients
├── requirements.txt
├── README.md
└── arduino_bridge/
    └── arduino_bridge.ino       # Replacement Arduino sketch
```

The `dogecoin`, `wire_format`, and `ir_transport` modules each have a
`__main__` self-test you can run standalone:

```bash
python3 dogecoin.py
python3 wire_format.py
python3 ir_transport.py
```

---

## APIs used

Both keyless and free.

- **BlockCypher** (`api.blockcypher.com`) — primary. Returns UTXOs with
  `script` field and accepts pushed transactions.
- **chainz.cryptoid.info** — fallback. Used both for UTXO lookup and as a
  secondary broadcast endpoint.

If both fail you'll see the exact errors in the log box at the bottom of
the app.

---

# GBC-side wire format spec

If you're updating the DogeGB firmware to talk to this companion, this is
the contract you need to implement. Three layers, top to bottom:

1. **Application messages** (`wire_format.py`)
2. **Chunked transport** with CRC32 framing (`ir_transport.py`)
3. **Raw IR bytes** — unchanged from your reference implementation
   (500 µs pulse + 1 ms / 3 ms gaps, MSB-first, start bit + terminator,
   5 ms inter-byte gap)

> ⚠️ Your reference GBC code uses null-terminated strings
> (`ir_send_msg` / `ir_recv_msg`). Transaction data contains arbitrary
> bytes including `0x00`, so you need fixed-length raw send/receive
> functions instead. Sketches below.

## Layer 3 → 2: replacing null-terminated send/receive

You already have `ir_tx_byte` and `ir_rx_byte`. Just wrap them so they
operate on a fixed length instead of stopping at `\0`:

```c
void ir_send_n(const uint8_t *buf, uint16_t n) {
    disable_interrupts();
    for (uint16_t i = 0; i < n; i++) ir_tx_byte(buf[i]);
    enable_interrupts();
}

uint16_t ir_recv_n(uint8_t *buf, uint16_t maxlen, uint16_t first_timeout) {
    uint16_t i = 0, b;
    uint16_t timeout;
    disable_interrupts();

    timeout = first_timeout;
    while (!IR_LIGHT()) {
        if (--timeout == 0) { enable_interrupts(); return 0; }
    }
    if (!ir_rx_byte((uint8_t*)&b)) { enable_interrupts(); return 0; }
    buf[i++] = (uint8_t)b;

    while (i < maxlen) {
        timeout = 10000U;  // ~inter-byte tolerance
        while (!IR_LIGHT()) {
            if (--timeout == 0) goto done;
        }
        if (!ir_rx_byte((uint8_t*)&b)) goto done;
        buf[i++] = (uint8_t)b;
    }
done:
    enable_interrupts();
    return i;
}
```

## Layer 2: chunk format (over IR)

```
+------+------+------+-----+-------+-----+-----------+----------+
| 0xDC | 0x6E | TYPE | SEQ | TOTAL | LEN |   DATA    | CRC32 BE |
+------+------+------+-----+-------+-----+-----------+----------+
   1     1      1     1     1       1     LEN bytes      4
```

- Header is **6 bytes**. CRC32 is **4 bytes**, big-endian.
- CRC32 covers the **header + DATA** (10 + LEN − 4 = 6 + LEN bytes), using
  the IEEE 802.3 polynomial — same as `zlib.crc32` and the standard
  Ethernet CRC.
- `LEN` is at most **192**, so a chunk is at most **202 bytes** total.

### Chunk types

| TYPE | Name | Direction | Meaning |
| ---- | ---- | --------- | ------- |
| `0x01` | `DATA`  | both     | payload chunk; `SEQ` ∈ `[0, TOTAL-1]`, `TOTAL` = total chunks in message |
| `0x02` | `ACK`   | both     | `TOTAL=0`, `LEN=0`; `SEQ` = chunk being acknowledged |
| `0x03` | `NACK`  | both     | bad CRC / framing error; sender should resend |
| `0x04` | `PING`  | PC→GBC   | `TOTAL=0`, `LEN=0` |
| `0x05` | `PONG`  | GBC→PC   | reply to `PING` |

### GBC receive loop (pseudocode)

```c
uint8_t chunk[256];
uint16_t n = ir_recv_n(chunk, sizeof(chunk), 10000);
if (n < 10) goto nack;                     // too short to be a chunk
if (chunk[0] != 0xDC || chunk[1] != 0x6E) goto nack;
uint8_t type   = chunk[2];
uint8_t seq    = chunk[3];
uint8_t total  = chunk[4];
uint8_t length = chunk[5];
if (n != 6 + length + 4) goto nack;

uint32_t got_crc = (chunk[n-4]<<24) | (chunk[n-3]<<16) | (chunk[n-2]<<8) | chunk[n-1];
uint32_t want_crc = crc32(chunk, 6 + length);
if (got_crc != want_crc) goto nack;

// process based on `type`...
// if DATA: copy chunk[6..6+length] into reassembly buffer at offset
//          (cumulative based on seq), then send ACK.

uint8_t ack[10];
ack[0]=0xDC; ack[1]=0x6E; ack[2]=0x02; ack[3]=seq; ack[4]=0; ack[5]=0;
uint32_t c = crc32(ack, 6);
ack[6]=c>>24; ack[7]=c>>16; ack[8]=c>>8; ack[9]=c;
ir_send_n(ack, 10);
```

You'll need a small CRC32 implementation. The IEEE polynomial is `0xEDB88320`
(reflected). A 256-entry table is fastest but 1 KB; a bit-by-bit version
fits in a few dozen bytes. Reference:

```c
uint32_t crc32(const uint8_t *data, uint16_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}
```

### Reassembly

`TOTAL` is the same in every `DATA` chunk of a message. `SEQ` runs
`0..TOTAL-1`. Chunks within one message are always the same `LEN` except
possibly the last one, but **don't rely on that** — concatenate
`length` bytes from each chunk in `SEQ` order. After sending `ACK` for the
final chunk, hand the assembled buffer to the application layer.

## Layer 1: application messages

After reassembly the payload begins with a 3-byte envelope:

```
+------+--------+----------------+
| TYPE |  LEN BE (2 bytes)       |
+------+--------+----------------+
|              PAYLOAD            |
+--------------------------------+
```

| TYPE | Name | Direction | Payload |
| ---- | ---- | --------- | ------- |
| `0x01` | `TX_PROPOSAL` | PC→GBC | custom binary, see below |
| `0x02` | `SIGNED_TX`   | GBC→PC | raw signed Bitcoin-format tx bytes |
| `0x03` | `PING`        | both   | empty (or arbitrary) |
| `0x04` | `PONG`        | both   | empty (or echo of PING) |
| `0x05` | `ERROR`       | both   | UTF-8 error string |

`LEN` is the byte length of `PAYLOAD`, big-endian. It will not exceed
~1500 bytes for a typical transaction.

### TX_PROPOSAL payload (PC → GBC)

This is **not** a Bitcoin transaction. It's a structured form designed
for trivial GBC parsing. The GBC reconstructs the canonical Bitcoin tx,
computes the legacy SIGHASH_ALL preimage for each input, signs it with
ECDSA, and assembles the final scriptSig.

All multi-byte integers are **big-endian**.

```
[1]    wire format version              = 0x01
[4 BE] tx version                       (Bitcoin tx version, normally 1)
[4 BE] locktime                         (normally 0)

[1]    number of inputs N               (1..8)
For each input:
    [32]   prev txid                    (block-explorer / display order;
                                         GBC must REVERSE it when
                                         serializing the canonical tx)
    [4 BE] prev vout
    [8 BE] value in koinu               (1 DOGE = 100,000,000 koinu)
    [1]    prev scriptPubKey length
    [...]  prev scriptPubKey

[1]    number of outputs M              (1..4)
For each output:
    [8 BE] value in koinu
    [1]    scriptPubKey length
    [...]  scriptPubKey

[4 BE] sighash type                     = 0x00000001 (SIGHASH_ALL)
```

### Signing on the GBC

For each input *i*, build the legacy preimage:

1. Start from the canonical unsigned tx (Bitcoin serialization, all
   multi-byte fields **little-endian**, txid bytes **reversed** from
   display order, varints for counts).
2. Replace input *i*'s `scriptSig` with the **prev scriptPubKey** for
   that input (from the proposal).
3. Set every other input's `scriptSig` to empty.
4. Append `sighash_type` as 4 bytes little-endian.
5. Hash with double-SHA-256.
6. Sign with the private key for the address that owns this UTXO. Use
   low-S DER encoding.
7. The final `scriptSig` is `<sig||sighash_byte> <pubkey>` (push of the
   DER signature concatenated with the 1-byte sighash type, then push of
   the 33-byte compressed pubkey).

### SIGNED_TX payload (GBC → PC)

Just the raw signed transaction bytes, exactly what would be hex-encoded
and pushed to a node. The companion broadcasts this unchanged.

---

## PC ↔ Arduino USB protocol

If you ever want to talk to the bridge directly (e.g. for debugging) it's
ASCII line-based at **115200 baud, 8-N-1**:

| PC sends                 | Arduino replies               | Meaning |
| ------------------------ | ----------------------------- | ------- |
| `PNG\n`                  | `<PONG\n`                     | health check |
| `TXR <ms> <hex>\n`       | `<RXOK <hex>\n` or `<TIMEOUT\n` | transmit hex bytes, then listen up to *ms* milliseconds for first reply byte |
| `RX <ms>\n`              | `<RXOK <hex>\n` or `<TIMEOUT\n` | listen-only |

Errors come back as `<ERR <reason>\n`. Debug logs are prefixed `# ` and
the host ignores them.

The Arduino is intentionally dumb. It does no framing, no CRC, no
retries — those all live in the Python code so the firmware can be a
single small file.

---

## Tuning notes

- **Chunk data size** is 64 bytes by default (`ir_transport.py`). You
  can raise this up to 192 (`MAX_CHUNK_DATA`); larger chunks mean fewer
  ACKs but a single bit error wastes more bytes. 64 is a good balance
  on a noisy IR link.
- **ACK timeout** is 1500 ms per chunk. The GBC has to drain the
  receive loop, validate the CRC, and turn the IR LED around to send
  the ACK — at full chunk size that's well under 500 ms, so 1500 ms
  leaves comfortable headroom.
- **Retries** are 4 per chunk. After that the transport surfaces a
  `TransportError` and the UI shows it in red.
- **Fee rate** defaults to 1 DOGE/kB. Editable in the UI. Coin
  selection is greedy largest-first; if the leftover change would be
  smaller than the dust threshold (0.01 DOGE) it's absorbed into the
  fee rather than created as an output.

---

## Disclaimer

This is unaudited software talking to unaudited firmware over an
unsynchronised IR link. **Test with tiny amounts first.** The companion
deliberately does not handle keys, but it *does* assemble the bytes the
GBC will sign — so verify the on-screen amount and address on the GBC
itself before confirming. That's the whole point of an air-gap.
