// ============================================================================
//  DogeGB IR Bridge - Arduino sketch
// ============================================================================
//
//  Sits between the host PC (via USB serial @ 115200 baud) and the Game Boy
//  Color (via pulse-distance IR). The Arduino is a *dumb byte pipe*: all
//  framing, CRC32, retries, and ACKs are handled by the Python companion app.
//  This sketch just sends/receives raw bytes over IR exactly as the GBC
//  expects them.
//
//  IR protocol (must match GBC firmware - unchanged from the reference code):
//    - Each bit  = 500us ON pulse + variable OFF gap
//        bit 0 = 500us pulse + 1000us gap
//        bit 1 = 500us pulse + 3000us gap
//    - Each byte = start bit (always 1) + 8 data bits MSB-first + terminator
//                  pulse + 5ms inter-byte gap
//
//  USB line protocol (PC <-> Arduino):
//    All commands are ASCII, terminated with '\n'. Hex bytes are lowercase
//    or uppercase, no separators. Responses begin with '<'. Debug logs
//    begin with '#' so the host can ignore them.
//
//    Commands from PC:
//      PNG\n
//          -> health check. Replies <PONG\n.
//      TXR <ack_timeout_ms> <hex>\n
//          -> transmit the hex bytes over IR, then listen for an inbound
//             chunk (up to ack_timeout_ms milliseconds for the first byte).
//             Replies <RXOK <hex>\n on success, <TIMEOUT\n if nothing is
//             heard. Used for the normal "send chunk, wait for ACK" flow.
//      RX <timeout_ms>\n
//          -> listen-only for an inbound chunk (no transmit). Same replies
//             as TXR. Used for receiving chunks the host did not solicit
//             (e.g. signed transactions coming back from the GBC).
//
//    Responses from Arduino:
//      <PONG\n               (reply to PNG)
//      <RXOK <hex>\n         (received some bytes from the GBC)
//      <TIMEOUT\n            (no IR activity within the timeout)
//      <ERR <reason>\n       (malformed command or buffer overflow)
//      # <text>\n            (debug/info, host should ignore)
//
//  The host decides chunk boundaries; the Arduino just reads bytes until
//  the inter-byte gap times out, then returns what it got.
//
// ============================================================================

#include <Arduino.h>
#include <stdint.h>

const int   IR_LED_PIN          = 2;
const int   IR_SENSOR_PIN       = A6;
const long  USB_BAUD            = 115200;

// Receive tuning. The GBC sends bytes back-to-back with a ~5ms inter-byte
// gap (start bit takes ~3.5ms, terminator + gap another ~5ms), so once the
// first byte arrives we treat ~250ms of silence as "chunk complete".
const unsigned long INTERBYTE_TIMEOUT_MS = 250;

// Maximum chunk payload supported. The Python transport uses 64-byte data
// chunks by default; the framed chunk on the wire is always under 256 bytes
// (6 header + up to 192 data + 4 CRC = 202 max). We round up generously.
const int   IR_BUF_MAX          = 256;

// Command line buffer. A 256-byte chunk in hex is 512 chars + small command
// prefix; round up.
const int   LINE_BUF_MAX        = 600;

// IR detection threshold above ambient (raw analogRead units).
const int   IR_THRESHOLD_OFFSET = 40;

// Inter-bit timeouts for receive (microseconds). Generous bounds so jitter
// doesn't cause spurious failures.
const unsigned long PULSE_END_TIMEOUT_US = 2000;
const unsigned long GAP_END_TIMEOUT_US   = 5000;
const unsigned long GAP_THRESHOLD_US     = 2000;

// ----------------------------------------------------------------------------
//  Globals
// ----------------------------------------------------------------------------

static int  ambient = 512;
static char lineBuf[LINE_BUF_MAX];
static int  lineLen = 0;

static uint8_t  irBuf[IR_BUF_MAX];

// ----------------------------------------------------------------------------
//  IR primitives - identical timing to the reference sketch
// ----------------------------------------------------------------------------

static inline bool irDetect() {
    return analogRead(IR_SENSOR_PIN) > (ambient + IR_THRESHOLD_OFFSET);
}

static void calibrate() {
    digitalWrite(IR_LED_PIN, LOW);
    delay(50);
    long sum = 0;
    for (int i = 0; i < 32; i++) {
        sum += analogRead(IR_SENSOR_PIN);
        delay(2);
    }
    ambient = sum / 32;
}

static void irSendBit(uint8_t bit) {
    digitalWrite(IR_LED_PIN, HIGH);
    delayMicroseconds(500);
    digitalWrite(IR_LED_PIN, LOW);
    delayMicroseconds(bit ? 3000 : 1000);
}

static void irSendByte(uint8_t val) {
    irSendBit(1);  // start bit
    for (int i = 7; i >= 0; i--) {
        irSendBit((val >> i) & 1);
    }
    // Terminator pulse
    digitalWrite(IR_LED_PIN, HIGH);
    delayMicroseconds(500);
    digitalWrite(IR_LED_PIN, LOW);
    // Inter-byte gap
    delayMicroseconds(5000);
}

// Send a raw fixed-length byte buffer (no null termination).
static void irSendN(const uint8_t *buf, int n) {
    noInterrupts();
    for (int i = 0; i < n; i++) {
        irSendByte(buf[i]);
    }
    interrupts();
}

// Receive a single byte. Caller must already have detected the rising
// edge of the start bit. Returns -1 on timeout/sync error.
static int irRecvByte() {
    uint8_t val = 0;
    unsigned long t;

    for (int i = 0; i < 9; i++) {  // 1 start + 8 data bits
        // Wait for falling edge
        t = micros();
        while (irDetect()) {
            if (micros() - t > PULSE_END_TIMEOUT_US) return -1;
        }
        // Measure gap to next rising edge
        unsigned long gapStart = micros();
        while (!irDetect()) {
            if (micros() - gapStart > GAP_END_TIMEOUT_US) return -1;
        }
        unsigned long gap = micros() - gapStart;

        if (i == 0) {
            // Start bit must be long
            if (gap < GAP_THRESHOLD_US) return -1;
        } else {
            val = (val << 1) | ((gap > GAP_THRESHOLD_US) ? 1 : 0);
        }
    }

    // Wait for terminator pulse to end
    t = micros();
    while (irDetect()) {
        if (micros() - t > PULSE_END_TIMEOUT_US) return -1;
    }
    return val;
}

// Receive raw bytes until inter-byte gap times out or buffer fills.
// firstTimeoutMs: how long to wait for the very first byte.
// Returns number of bytes received (0 = timeout on first byte).
static int irRecvN(uint8_t *buf, int maxlen, unsigned long firstTimeoutMs) {
    int i = 0;

    // Wait for the rising edge of the first start bit
    unsigned long start = millis();
    while (!irDetect()) {
        if (millis() - start > firstTimeoutMs) return 0;
    }

    int b = irRecvByte();
    if (b < 0) return 0;
    buf[i++] = (uint8_t)b;

    // Subsequent bytes - bounded inter-byte gap
    while (i < maxlen) {
        unsigned long t = millis();
        bool gotEdge = false;
        while (millis() - t < INTERBYTE_TIMEOUT_MS) {
            if (irDetect()) { gotEdge = true; break; }
        }
        if (!gotEdge) break;  // chunk complete

        b = irRecvByte();
        if (b < 0) break;     // sync error -> stop, return what we have
        buf[i++] = (uint8_t)b;
    }
    return i;
}

// ----------------------------------------------------------------------------
//  Hex helpers
// ----------------------------------------------------------------------------

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Decode `len` hex chars starting at `src` into `dst`. Returns number of
// bytes written, or -1 on error / odd length / overflow.
static int hexDecode(const char *src, int len, uint8_t *dst, int dstMax) {
    if (len & 1) return -1;
    int outLen = len / 2;
    if (outLen > dstMax) return -1;
    for (int i = 0; i < outLen; i++) {
        int hi = hexNibble(src[i * 2]);
        int lo = hexNibble(src[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        dst[i] = (uint8_t)((hi << 4) | lo);
    }
    return outLen;
}

static void printHexByte(uint8_t b) {
    static const char HEX_DIGITS[] = "0123456789abcdef";
    Serial.write(HEX_DIGITS[(b >> 4) & 0xF]);
    Serial.write(HEX_DIGITS[b & 0xF]);
}

static void printHexBuf(const uint8_t *buf, int n) {
    for (int i = 0; i < n; i++) printHexByte(buf[i]);
}

// ----------------------------------------------------------------------------
//  Command handlers
// ----------------------------------------------------------------------------

static void respondRxResult(int n) {
    if (n > 0) {
        Serial.print(F("<RXOK "));
        printHexBuf(irBuf, n);
        Serial.print('\n');
    } else {
        Serial.print(F("<TIMEOUT\n"));
    }
}

static void handlePing() {
    Serial.print(F("<PONG\n"));
}

// TXR <ack_timeout_ms> <hex>
static void handleTxR(const char *args) {
    // Parse timeout
    char *endp;
    long timeoutMs = strtol(args, &endp, 10);
    if (endp == args || *endp != ' ' || timeoutMs < 0 || timeoutMs > 600000) {
        Serial.print(F("<ERR bad_timeout\n"));
        return;
    }
    const char *hex = endp + 1;
    int hexLen = strlen(hex);
    int n = hexDecode(hex, hexLen, irBuf, IR_BUF_MAX);
    if (n < 0) {
        Serial.print(F("<ERR bad_hex\n"));
        return;
    }

    // Transmit
    irSendN(irBuf, n);

    // Tiny pause so the GBC has time to switch from RX to TX
    delay(40);

    // Listen for reply
    int got = irRecvN(irBuf, IR_BUF_MAX, (unsigned long)timeoutMs);
    respondRxResult(got);
}

// RX <timeout_ms>
static void handleRx(const char *args) {
    char *endp;
    long timeoutMs = strtol(args, &endp, 10);
    if (endp == args || *endp != '\0' || timeoutMs < 0 || timeoutMs > 600000) {
        Serial.print(F("<ERR bad_timeout\n"));
        return;
    }
    int got = irRecvN(irBuf, IR_BUF_MAX, (unsigned long)timeoutMs);
    respondRxResult(got);
}

static void dispatch(char *line) {
    // Strip trailing CR
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
    if (len == 0) return;

    if (strcmp(line, "PNG") == 0) {
        handlePing();
    } else if (strncmp(line, "TXR ", 4) == 0) {
        handleTxR(line + 4);
    } else if (strncmp(line, "RX ", 3) == 0) {
        handleRx(line + 3);
    } else {
        Serial.print(F("<ERR unknown_cmd\n"));
    }
}

// ----------------------------------------------------------------------------
//  Main
// ----------------------------------------------------------------------------

void setup() {
    pinMode(IR_LED_PIN, OUTPUT);
    digitalWrite(IR_LED_PIN, LOW);
    Serial.begin(USB_BAUD);
    delay(200);
    calibrate();
    Serial.print(F("# DogeGB IR Bridge ready, ambient="));
    Serial.println(ambient);
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            dispatch(lineBuf);
            lineLen = 0;
        } else if (lineLen < LINE_BUF_MAX - 1) {
            lineBuf[lineLen++] = c;
        } else {
            // Overflow - reset and report
            lineLen = 0;
            Serial.print(F("<ERR line_overflow\n"));
        }
    }
}
