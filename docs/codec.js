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
  priceFixed,
  selectCoins,
};
})();
