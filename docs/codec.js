// DogeGB typed-payment string, format version 1.
//
// The page builds these bytes and base58-encodes them. Every line is 8
// characters. The 8th is the mod-58 sum of the first 7. A payment line uses
// that sum. The checksum line adds 1..7, and the amount added is how many
// payment characters sit at the front of that line (0..6). Two to six
// leftover payment characters make the checksum two lines. The hash fills
// every spare character.
//
// Bytes, big-endian:
//   0x01            version
//   u8              input count, 1..4
//   repeated:
//     32            txid, block-explorer order
//     u16           vout
//     u64           value, koinu
//   u64             pay value, koinu
//   u32             fee, koinu
//   u8              0 = pay-to-address, 1 = pay-to-script
//   20              hash160 of the destination
//
// Change is not in the string. It is the inputs minus the payment minus
// the fee, and it pays this wallet. Version, locktime, and sequence stay
// at 1, 0, and 0xffffffff and are not in the string.

globalThis.DogeCodec = (function () {
const ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const KOINU = 100_000_000n;
const DUST = 1_000_000n;
const MAX_INPUTS = 4;
const FORMAT_VERSION = 0x01;
// Hash characters available for leftover counts 0..6, then the most bytes
// that still fit in that many base58 digits.
const HASH_CHARS = [7, 6, 12, 11, 10, 9, 8];
const HASH_BYTES = [5, 4, 8, 8, 7, 6, 5];

const P2PKH = 0x1e;
const P2SH = 0x16;

function alphabetIndex(ch) {
  const i = ALPHABET.indexOf(ch);
  if (i < 0) throw new Error("Character " + ch + " is not on the base58 keyboard.");
  return i;
}

function b58encode(data) {
  let n = 0n;
  for (const b of data) n = (n << 8n) + BigInt(b);
  let out = "";
  while (n > 0n) {
    const r = n % 58n;
    n /= 58n;
    out = ALPHABET[Number(r)] + out;
  }
  for (const b of data) {
    if (b === 0) out = "1" + out;
    else break;
  }
  return out || "1";
}

function b58decode(text) {
  let n = 0n;
  for (const ch of text) n = n * 58n + BigInt(alphabetIndex(ch));
  let body = [];
  while (n > 0n) {
    body.push(Number(n & 0xffn));
    n >>= 8n;
  }
  body.reverse();
  let zeros = 0;
  for (const ch of text) {
    if (ch === "1") zeros++;
    else break;
  }
  const raw = new Uint8Array(zeros + body.length);
  raw.set(body, zeros);
  return raw;
}

async function sha256(data) {
  const digest = await crypto.subtle.digest("SHA-256", data);
  return new Uint8Array(digest);
}

async function sha256d(data) {
  return sha256(await sha256(data));
}

function b58checkDecode(text) {
  const raw = b58decode(text.trim());
  if (raw.length < 5) throw new Error("Address is too short.");
  return { body: raw.slice(0, -4), checksum: raw.slice(-4) };
}

const ADDRESS_RUN = /[1-9A-HJ-NP-Za-km-z]{25,44}/;

function extractPayment(raw) {
  let text = String(raw).replace(/\0/g, " ").trim();
  let amount = null;
  const uri = text.match(/dogecoin:([^?\s]+)(?:\?(\S+))?/i);
  if (uri) {
    text = decodeURIComponent(uri[1]);
    if (uri[2]) {
      const amountText = new URLSearchParams(uri[2]).get("amount");
      if (amountText) amount = amountText;
    }
  }
  const found = text.match(ADDRESS_RUN);
  if (!found) throw new Error("That code is not a Dogecoin address.");
  return { address: found[0], amount };
}

async function decodeAddress(text) {
  const trimmed = text.trim();
  const { body, checksum } = b58checkDecode(trimmed);
  const hash = await sha256d(body);
  for (let i = 0; i < 4; i++) {
    if (hash[i] !== checksum[i]) throw new Error("Address checksum does not match.");
  }
  if (body.length !== 21) throw new Error("Address payload is the wrong length.");
  const version = body[0];
  const hash160 = body.slice(1);
  if (version === P2PKH) return { kind: "p2pkh", typeByte: 0, hash160, text: trimmed };
  if (version === P2SH) return { kind: "p2sh", typeByte: 1, hash160, text: trimmed };
  throw new Error("Address version 0x" + version.toString(16) + " is not Dogecoin.");
}

function scriptP2pkh(hash160) {
  const out = new Uint8Array(25);
  out[0] = 0x76;
  out[1] = 0xa9;
  out[2] = 20;
  out.set(hash160, 3);
  out[23] = 0x88;
  out[24] = 0xac;
  return out;
}

function dogeToKoinu(text) {
  const s = String(text).trim();
  if (!/^\d+(\.\d{0,8})?$/.test(s)) {
    throw new Error("Amount must be a DOGE number with at most 8 decimals.");
  }
  const [whole, frac = ""] = s.split(".");
  const padded = (frac + "00000000").slice(0, 8);
  return BigInt(whole) * KOINU + BigInt(padded);
}

function formatDoge(koinu) {
  const neg = koinu < 0n;
  const v = neg ? -koinu : koinu;
  const whole = v / KOINU;
  const frac = (v % KOINU).toString().padStart(8, "0");
  return (neg ? "-" : "") + whole.toString() + "." + frac;
}

function feeRatePerByte(dogePerKb) {
  const perKb = dogeToKoinu(dogePerKb);
  const q = perKb / 1000n;
  const r = perKb % 1000n;
  const rate = q + (r >= 500n ? 1n : 0n);
  return rate < 1n ? 1n : rate;
}

function estimateSize(numInputs, recipientIsP2sh, withChange) {
  const p2pkh = (recipientIsP2sh ? 0 : 1) + (withChange ? 1 : 0);
  const p2sh = recipientIsP2sh ? 1 : 0;
  return 10 + numInputs * 148 + p2pkh * 34 + p2sh * 32;
}

function pushU16(out, n) {
  if (!Number.isInteger(n) || n < 0 || n > 0xffff) {
    throw new Error("Output index must be 0..65535.");
  }
  out.push((n >> 8) & 255, n & 255);
}

function pushU32(out, n) {
  if (n < 0n || n > 0xffffffffn) throw new Error("Fee is too large to encode.");
  let x = n;
  const b = [0, 0, 0, 0];
  for (let i = 3; i >= 0; i--) {
    b[i] = Number(x & 0xffn);
    x >>= 8n;
  }
  out.push(...b);
}

function pushU64(out, n) {
  if (n < 0n || n > 0xffffffffffffffffn) throw new Error("Value does not fit in 8 bytes.");
  let x = n;
  const b = [0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 7; i >= 0; i--) {
    b[i] = Number(x & 0xffn);
    x >>= 8n;
  }
  out.push(...b);
}

function readU16(bytes, p) {
  return (bytes[p] << 8) | bytes[p + 1];
}

function readU32(bytes, p) {
  return (BigInt(bytes[p]) << 24n) | (BigInt(bytes[p + 1]) << 16n) |
    (BigInt(bytes[p + 2]) << 8n) | BigInt(bytes[p + 3]);
}

function readU64(bytes, p) {
  let n = 0n;
  for (let i = 0; i < 8; i++) n = (n << 8n) | BigInt(bytes[p + i]);
  return n;
}

function encodePayload(payment) {
  const inputs = payment.inputs;
  if (inputs.length < 1 || inputs.length > MAX_INPUTS) {
    throw new Error("Use 1 to " + MAX_INPUTS + " coins.");
  }
  const out = [];
  out.push(FORMAT_VERSION);
  out.push(inputs.length);
  let total = 0n;
  for (const inp of inputs) {
    const txid = inp.txid.toLowerCase();
    if (!/^[0-9a-f]{64}$/.test(txid)) throw new Error("A txid must be 64 hex characters.");
    for (let i = 0; i < 64; i += 2) out.push(parseInt(txid.slice(i, i + 2), 16));
    pushU16(out, inp.vout);
    pushU64(out, inp.value);
    total += inp.value;
  }
  if (payment.pay <= 0n) throw new Error("Amount must be more than 0.");
  if (payment.fee < 0n) throw new Error("Fee is negative.");
  if (total !== payment.pay + payment.fee + payment.change) {
    throw new Error("Coins, payment, fee, and change do not add up.");
  }
  pushU64(out, payment.pay);
  pushU32(out, payment.fee);
  if (payment.typeByte !== 0 && payment.typeByte !== 1) {
    throw new Error("Destination type is unknown.");
  }
  out.push(payment.typeByte);
  if (payment.hash160.length !== 20) throw new Error("Destination hash is the wrong length.");
  for (const b of payment.hash160) out.push(b);
  return new Uint8Array(out);
}

function decodePayload(bytes) {
  let p = 0;
  const need = (n) => {
    if (p + n > bytes.length) throw new Error("String ended early.");
    const s = bytes.slice(p, p + n);
    p += n;
    return s;
  };
  const version = need(1)[0];
  if (version !== FORMAT_VERSION) throw new Error("Unknown string version.");
  const n = need(1)[0];
  if (n < 1 || n > MAX_INPUTS) throw new Error("Input count is out of range.");
  const inputs = [];
  let total = 0n;
  for (let i = 0; i < n; i++) {
    const txidBytes = need(32);
    let txid = "";
    for (const b of txidBytes) txid += b.toString(16).padStart(2, "0");
    const vout = readU16(need(2), 0);
    const value = readU64(need(8), 0);
    total += value;
    inputs.push({ txid, vout, value });
  }
  const pay = readU64(need(8), 0);
  const fee = readU32(need(4), 0);
  const typeByte = need(1)[0];
  const hash160 = need(20);
  if (p !== bytes.length) throw new Error("String has extra bytes.");
  const change = total - pay - fee;
  return { inputs, pay, fee, change, typeByte, hash160 };
}

function sumIndex(chunk) {
  let sum = 0;
  for (const ch of chunk) sum += alphabetIndex(ch);
  return sum;
}

function checkChar(chunk, delta) {
  return ALPHABET[(sumIndex(chunk) + delta) % 58];
}

function groupDelta(group) {
  if (group.length !== 8) throw new Error("A group must be 8 characters.");
  const got = alphabetIndex(group[7]);
  const expect = sumIndex(group.slice(0, 7)) % 58;
  return (got - expect + 58) % 58;
}

function encodeFixed(bytes, width) {
  let n = 0n;
  for (const b of bytes) n = (n << 8n) + BigInt(b);
  let out = "";
  for (let i = 0; i < width; i++) {
    const r = n % 58n;
    n /= 58n;
    out = ALPHABET[Number(r)] + out;
  }
  if (n !== 0n) throw new Error("End hash does not fit.");
  return out;
}

function decodeFixed(text, nbytes) {
  let n = 0n;
  for (const ch of text) n = n * 58n + BigInt(alphabetIndex(ch));
  const out = new Uint8Array(nbytes);
  for (let i = nbytes - 1; i >= 0; i--) {
    out[i] = Number(n & 0xffn);
    n >>= 8n;
  }
  if (n !== 0n) throw new Error("End check is too large.");
  return out;
}

function encodeGroups(dataChars, hashBytes) {
  const r = dataChars.length % 7;
  const groups = [];
  const full = dataChars.slice(0, dataChars.length - r);
  for (let i = 0; i < full.length; i += 7) {
    const chunk = full.slice(i, i + 7);
    groups.push(chunk + checkChar(chunk, 0));
  }
  const hashText = encodeFixed(hashBytes, HASH_CHARS[r]);
  if (r <= 1) {
    const line = dataChars.slice(full.length) + hashText;
    groups.push(line + checkChar(line, r + 1));
    return groups;
  }
  const firstHash = 7 - r;
  const line = dataChars.slice(full.length) + hashText.slice(0, firstHash);
  const rest = hashText.slice(firstHash);
  groups.push(line + checkChar(line, r + 1));
  groups.push(rest + checkChar(rest, 0));
  return groups;
}

async function encodeString(payment) {
  const payload = encodePayload(payment);
  const dataChars = b58encode(payload);
  const r = dataChars.length % 7;
  const hashBytes = HASH_BYTES[r];
  const groups = encodeGroups(dataChars, (await sha256d(payload)).slice(0, hashBytes));
  return {
    payload,
    groups,
    hashBytes,
    typedCount: groups.length * 8,
  };
}

function stripTyped(text) {
  return text.replace(/\s+/g, "");
}

async function decodeString(text) {
  const raw = stripTyped(text);
  if (raw.length < 8 || raw.length % 8 !== 0) throw new Error("String length is wrong.");
  let data = "";
  let hashText = null;
  let hashBytes = 0;
  let i = 0;
  let groupNo = 1;
  while (i < raw.length) {
    if (hashText !== null) throw new Error("String has extra characters.");
    const group = raw.slice(i, i + 8);
    const delta = groupDelta(group);
    if (delta === 0) {
      data += group.slice(0, 7);
      i += 8;
      groupNo++;
      continue;
    }
    if (delta > 7) throw new Error("Group " + groupNo + " does not check.");
    const r = delta - 1;
    data += group.slice(0, r);
    hashText = group.slice(r, 7);
    i += 8;
    groupNo++;
    if (r >= 2) {
      if (i >= raw.length) throw new Error("The end check is cut off.");
      const next = raw.slice(i, i + 8);
      if (groupDelta(next) !== 0) throw new Error("Group " + groupNo + " does not check.");
      hashText += next.slice(0, 7);
      i += 8;
    }
    if (hashText.length !== HASH_CHARS[r]) throw new Error("The end check is the wrong length.");
    hashBytes = HASH_BYTES[r];
  }
  if (hashText === null) throw new Error("The end check is missing.");
  const payload = b58decode(data);
  const got = decodeFixed(hashText, hashBytes);
  const want = (await sha256d(payload)).slice(0, hashBytes);
  for (let b = 0; b < hashBytes; b++) {
    if (want[b] !== got[b]) throw new Error("The end check does not match.");
  }
  return decodePayload(payload);
}

function priceFixed(numInputs, totalIn, sendValue, feeRate, recipientIsP2sh) {
  const withChange = estimateSize(numInputs, recipientIsP2sh, true);
  const feeWith = BigInt(withChange) * feeRate;
  const change = totalIn - sendValue - feeWith;
  if (change >= DUST) {
    return { fee: feeWith, change, size: withChange, totalIn };
  }
  const noChangeSize = estimateSize(numInputs, recipientIsP2sh, false);
  const feeNo = BigInt(noChangeSize) * feeRate;
  if (totalIn === sendValue + feeNo) {
    return { fee: feeNo, change: 0n, size: noChangeSize, totalIn };
  }
  if (totalIn >= sendValue + feeNo && change < DUST) {
    const fee = totalIn - sendValue;
    if (fee > 0xffffffffn) throw new Error("Fee is too large to encode.");
    return { fee, change: 0n, size: noChangeSize, totalIn };
  }
  return null;
}

// The signed codes are version 3. Byte 0 is the page, byte 1 is how many
// pages, and the rest is one slice of the blob. The blob is the 33-byte
// compressed pubkey, then each signature in input order. A signature is
// DER and already ends with the SIGHASH_ALL byte, which sits outside the
// DER length. At most 51 bytes of the blob fit in one code.
const SIG_PAGE_DATA = 51;
const SIG_PAGE_MAX = 7;

function ripemd160(message) {
  const ml = message.length;
  const withPad = ml + ((ml % 64 < 56) ? (56 - (ml % 64)) : (120 - (ml % 64))) + 8;
  const padded = new Uint8Array(withPad);
  padded.set(message);
  padded[ml] = 0x80;
  const view = new DataView(padded.buffer);
  const bits = ml * 8;
  view.setUint32(withPad - 8, bits >>> 0, true);
  view.setUint32(withPad - 4, Math.floor(bits / 0x100000000), true);

  let h0 = 0x67452301;
  let h1 = 0xefcdab89;
  let h2 = 0x98badcfe;
  let h3 = 0x10325476;
  let h4 = 0xc3d2e1f0;
  const r1 = [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
    3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
    1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
    4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13,
  ];
  const r2 = [
    5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
    6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
    15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
    8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
    12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11,
  ];
  const s1 = [
    11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
    7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
    11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
    11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
    9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6,
  ];
  const s2 = [
    8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
    9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
    9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
    15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
    8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11,
  ];
  const k1 = [0x00000000, 0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xa953fd4e];
  const k2 = [0x50a28be6, 0x5c4dd124, 0x6d703ef3, 0x7a6d76e9, 0x00000000];
  const rotl = (x, n) => (x << n) | (x >>> (32 - n));
  const f = (j, x, y, z) => {
    if (j < 16) return x ^ y ^ z;
    if (j < 32) return (x & y) | (~x & z);
    if (j < 48) return (x | ~y) ^ z;
    if (j < 64) return (x & z) | (y & ~z);
    return x ^ (y | ~z);
  };

  for (let off = 0; off < padded.length; off += 64) {
    const x = new Uint32Array(16);
    for (let i = 0; i < 16; i++) x[i] = view.getUint32(off + i * 4, true);
    let al = h0, bl = h1, cl = h2, dl = h3, el = h4;
    let ar = h0, br = h1, cr = h2, dr = h3, er = h4;
    for (let j = 0; j < 80; j++) {
      const sl = (rotl((al + f(j, bl, cl, dl) + x[r1[j]] + k1[j >>> 4]) | 0, s1[j]) + el) | 0;
      al = el; el = dl; dl = rotl(cl, 10) | 0; cl = bl; bl = sl;
      const sr = (rotl((ar + f(79 - j, br, cr, dr) + x[r2[j]] + k2[j >>> 4]) | 0, s2[j]) + er) | 0;
      ar = er; er = dr; dr = rotl(cr, 10) | 0; cr = br; br = sr;
    }
    const t = (h1 + cl + dr) | 0;
    h1 = (h2 + dl + er) | 0;
    h2 = (h3 + el + ar) | 0;
    h3 = (h4 + al + br) | 0;
    h4 = (h0 + bl + cr) | 0;
    h0 = t;
  }
  const out = new Uint8Array(20);
  const words = new DataView(out.buffer);
  words.setUint32(0, h0, true);
  words.setUint32(4, h1, true);
  words.setUint32(8, h2, true);
  words.setUint32(12, h3, true);
  words.setUint32(16, h4, true);
  return out;
}

async function hash160(data) {
  return ripemd160(await sha256(data));
}

function parseSigPage(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = Uint8Array.from(bytes);
  if (bytes.length < 3 || bytes.length > SIG_PAGE_DATA + 2) {
    throw new Error("That code is not a signed page.");
  }
  const page = bytes[0];
  const count = bytes[1];
  if (count < 1 || count > SIG_PAGE_MAX || page >= count) {
    throw new Error("That code is not a signed page.");
  }
  const data = bytes.slice(2);
  if (data.length === 0 || (page + 1 < count && data.length !== SIG_PAGE_DATA)) {
    throw new Error("That code is not a signed page.");
  }
  return { page, count, data };
}

function assembleSigBlob(pages) {
  if (!pages.length) throw new Error("Scan the signed codes.");
  const count = pages[0].count;
  if (pages.length !== count) throw new Error("Scan every signed code.");
  const got = new Array(count);
  for (const part of pages) {
    if (part.count !== count || part.page >= count || got[part.page]) {
      throw new Error("Those codes are not one set.");
    }
    if (part.page + 1 < count && part.data.length !== SIG_PAGE_DATA) {
      throw new Error("That code is not a signed page.");
    }
    got[part.page] = part.data;
  }
  let n = 0;
  for (const part of got) n += part.length;
  const blob = new Uint8Array(n);
  let at = 0;
  for (const part of got) {
    blob.set(part, at);
    at += part.length;
  }
  return blob;
}

function readSigs(blob) {
  if (blob.length < 34) throw new Error("The codes are too short.");
  const pub = blob.slice(0, 33);
  if (pub[0] !== 2 && pub[0] !== 3) throw new Error("The key on the codes is not a compressed key.");
  const sigs = [];
  let p = 33;
  while (p < blob.length) {
    if (blob[p] !== 0x30 || p + 2 > blob.length) throw new Error("A signature on the codes is damaged.");
    const body = blob[p + 1];
    const total = 2 + body + 1;
    if (body < 6 || total > 73 || p + total > blob.length || blob[p + total - 1] !== 0x01) {
      throw new Error("A signature on the codes is damaged.");
    }
    sigs.push(blob.slice(p, p + total));
    p += total;
  }
  if (sigs.length < 1 || sigs.length > MAX_INPUTS) {
    throw new Error("The codes do not match this payment.");
  }
  return { pub, sigs };
}

function scriptFor(typeByte, hash160Bytes) {
  if (typeByte === 1) {
    const out = new Uint8Array(23);
    out[0] = 0xa9;
    out[1] = 0x14;
    out.set(hash160Bytes, 2);
    out[22] = 0x87;
    return out;
  }
  if (typeByte !== 0) throw new Error("Destination type is unknown.");
  return scriptP2pkh(hash160Bytes);
}

function pushScript(out, script) {
  if (script.length >= 253) throw new Error("A script is too long.");
  out.push(script.length);
  for (const b of script) out.push(b);
}

function le32(out, n) {
  const x = Number(n);
  out.push(x & 255, (x >>> 8) & 255, (x >>> 16) & 255, (x >>> 24) & 255);
}

function le64(out, n) {
  let x = BigInt(n);
  for (let i = 0; i < 8; i++) {
    out.push(Number(x & 0xffn));
    x >>= 8n;
  }
}

async function buildSignedTx(payment, blob) {
  const { pub, sigs } = readSigs(blob);
  if (sigs.length !== payment.inputs.length) {
    throw new Error("The codes do not match this payment.");
  }
  const got = await hash160(pub);
  const want = payment.walletHash160;
  if (!want || got.length !== want.length) throw new Error("That key is not this Game Boy.");
  for (let i = 0; i < got.length; i++) {
    if (got[i] !== want[i]) throw new Error("That key is not this Game Boy.");
  }
  const hasChange = payment.change >= DUST;
  const out = [];
  le32(out, 1);
  out.push(payment.inputs.length);
  for (let i = 0; i < payment.inputs.length; i++) {
    const txid = String(payment.inputs[i].txid).toLowerCase();
    if (!/^[0-9a-f]{64}$/.test(txid)) throw new Error("A txid must be 64 hex characters.");
    for (let b = 62; b >= 0; b -= 2) out.push(parseInt(txid.slice(b, b + 2), 16));
    le32(out, payment.inputs[i].vout);
    const sig = sigs[i];
    const script = new Uint8Array(sig.length + pub.length + 2);
    script[0] = sig.length;
    script.set(sig, 1);
    script[1 + sig.length] = pub.length;
    script.set(pub, 2 + sig.length);
    pushScript(out, script);
    le32(out, 0xffffffff);
  }
  out.push(hasChange ? 2 : 1);
  le64(out, payment.pay);
  pushScript(out, scriptFor(payment.typeByte, payment.hash160));
  if (hasChange) {
    le64(out, payment.change);
    pushScript(out, scriptP2pkh(payment.walletHash160));
  }
  le32(out, 0);
  return out.map((b) => b.toString(16).padStart(2, "0")).join("");
}

function selectCoins(utxos, sendValue, feeRate, recipientIsP2sh) {
  if (sendValue <= 0n) throw new Error("Amount must be more than 0.");
  const sorted = utxos.filter((u) => u.spendable !== false).sort((a, b) => {
    if (a.value === b.value) return 0;
    return a.value > b.value ? -1 : 1;
  });
  const chosen = [];
  let total = 0n;
  for (const u of sorted) {
    if (chosen.length >= MAX_INPUTS) break;
    chosen.push(u);
    total += u.value;
    const plan = priceFixed(chosen.length, total, sendValue, feeRate, recipientIsP2sh);
    if (plan) return { chosen, ...plan };
  }
  throw new Error("The first 4 coins do not cover this payment and fee.");
}

return {
  ALPHABET,
  KOINU,
  DUST,
  MAX_INPUTS,
  FORMAT_VERSION,
  HASH_CHARS,
  HASH_BYTES,
  alphabetIndex,
  b58encode,
  b58decode,
  sha256d,
  b58checkDecode,
  extractPayment,
  decodeAddress,
  scriptP2pkh,
  dogeToKoinu,
  formatDoge,
  feeRatePerByte,
  estimateSize,
  encodePayload,
  decodePayload,
  encodeString,
  stripTyped,
  decodeString,
  ripemd160,
  hash160,
  parseSigPage,
  assembleSigBlob,
  buildSignedTx,
  priceFixed,
  selectCoins,
};
})();
