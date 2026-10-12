import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const sandbox = vm.createContext({
  crypto: globalThis.crypto,
  URLSearchParams: globalThis.URLSearchParams,
});
vm.runInContext(
  readFileSync(new URL("../docs/codec.js", import.meta.url), "utf8"),
  sandbox,
  { filename: "codec.js" }
);
const {
  decodeAddress,
  decodeString,
  dogeToKoinu,
  encodeString,
  extractPayment,
  feeRatePerByte,
  formatDoge,
  assembleSigBlob,
  buildSignedTx,
  hash160,
  parseSigPage,
  priceFixed,
  ripemd160,
  selectCoins,
} = sandbox.DogeCodec;

const known = "DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC";
assert.equal(extractPayment(known).address, known);
assert.equal(extractPayment("  " + known + "\n").address, known);
assert.equal(extractPayment("dogecoin:" + known + "?amount=1.5").amount, "1.5");
assert.equal(extractPayment("\0" + known).address, known);
assert.throws(() => extractPayment("hello"), /not a Dogecoin/);

const addr = await decodeAddress("DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC");
assert.equal(addr.kind, "p2pkh");
assert.equal(addr.typeByte, 0);
assert.equal(
  [...addr.hash160].map((b) => b.toString(16).padStart(2, "0")).join(""),
  "4a483568665dcdfa68dd58a1f62893448a643339"
);

assert.equal(dogeToKoinu("1"), 100_000_000n);
assert.equal(dogeToKoinu("1.5"), 150_000_000n);
assert.equal(dogeToKoinu("0.01"), 1_000_000n);
assert.equal(feeRatePerByte("1.0"), 100_000n);
assert.equal(feeRatePerByte("0.01"), 1_000n);
assert.equal(formatDoge(100_000_000n), "1.00000000");

const utxo = {
  txid: "ab".repeat(32),
  vout: 1,
  value: 1_000_000_000n,
  spendable: true,
};
const plan = priceFixed(1, utxo.value, 100_000_000n, 1000n, false);
assert.equal(plan.size, 226);
assert.equal(plan.fee, 226_000n);
assert.equal(plan.change, 899_774_000n);

const payment = {
  inputs: [utxo],
  pay: 100_000_000n,
  fee: plan.fee,
  change: plan.change,
  typeByte: 0,
  hash160: addr.hash160,
};
const encoded = await encodeString(payment);
assert.ok(encoded.groups.length >= 1);
assert.ok(encoded.groups.every((g) => g.length === 8));
const typed = encoded.groups.join("");
const back = await decodeString(typed);
assert.equal(back.inputs.length, 1);
assert.equal(back.inputs[0].txid, utxo.txid);
assert.equal(back.inputs[0].vout, 1);
assert.equal(back.inputs[0].value, undefined);
assert.equal(encoded.payload[0], 0);
assert.equal(encoded.payload.length, 73);
assert.equal(back.pay, payment.pay);
assert.equal(back.change, payment.change);

const chars = encoded.groups[0].split("");
const swap = chars.slice();
const tmp = swap[0];
swap[0] = swap[1];
swap[1] = tmp;
const swapped = swap.join("") + encoded.groups.slice(1).join("");
await assert.rejects(decodeString(swapped), /end check/);

const bad = chars.slice();
const nextCh = bad[0] === "2" ? "3" : "2";
bad[0] = nextCh;
const substituted = bad.join("") + encoded.groups.slice(1).join("");
await assert.rejects(decodeString(substituted), /Group 1|extra characters|end check/);

const spaced = encoded.groups.join("  ");
const fromSpaces = await decodeString(spaced);
assert.equal(fromSpaces.pay, payment.pay);

for (const n of [1, 2, 3, 4]) {
  const inputs = [];
  for (let i = 0; i < n; i++) {
    inputs.push({
      txid: (i + 1).toString(16).padStart(2, "0").repeat(32),
      vout: i,
      value: 1_000_000_000n,
    });
  }
  const total = 1_000_000_000n * BigInt(n);
  const planN = priceFixed(n, total, payment.pay, 1000n, false);
  const sample = {
    inputs,
    pay: payment.pay,
    fee: planN.fee,
    change: planN.change,
    typeByte: 0,
    hash160: addr.hash160,
  };
  const enc = await encodeString(sample);
  assert.ok(enc.groups.every((g) => g.length === 8));
  const dec = await decodeString(enc.groups.join(""));
  assert.equal(dec.inputs.length, n);
  assert.equal(dec.inputs[0].value, undefined);
  assert.equal(dec.pay, sample.pay);
  assert.equal(dec.change, planN.change);
  assert.equal(enc.payload.length, 2 + n * 34 + 37);
}
const p2shPlan = priceFixed(1, utxo.value, payment.pay, 1000n, true);
assert.equal(p2shPlan.fee, 224_000n);
const p2shEnc = await encodeString({
  inputs: [utxo],
  pay: payment.pay,
  fee: p2shPlan.fee,
  change: p2shPlan.change,
  typeByte: 1,
  hash160: addr.hash160,
});
const p2shBack = await decodeString(p2shEnc.groups.join(""));
assert.equal(p2shBack.change, p2shPlan.change);
assert.equal(p2shBack.typeByte, 1);

const picked = selectCoins(
  [
    { txid: "11".repeat(32), vout: 0, value: 50_000_000n, spendable: true },
    { txid: "22".repeat(32), vout: 0, value: 1_000_000_000n, spendable: true },
    { txid: "33".repeat(32), vout: 0, value: 10_000_000n, spendable: false },
  ],
  100_000_000n,
  1000n,
  false
);
assert.equal(picked.chosen.length, 1);
assert.equal(picked.chosen[0].txid, "22".repeat(32));
assert.equal(picked.change, 899_774_000n);

function hexOf(bytes) {
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}
assert.equal(hexOf(ripemd160(new Uint8Array())), "9c1185a5c5e9fc54612808977ee8f548b2258d31");
assert.equal(hexOf(ripemd160(new TextEncoder().encode("abc"))), "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");

const pub = new Uint8Array(33);
pub[0] = 2;
pub.fill(0x11, 1);
const wallet160 = await hash160(pub);
const dest160 = new Uint8Array(20).fill(0x22);
const sigA = Uint8Array.from([0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x02, 0x01]);
const sigB = Uint8Array.from([0x30, 0x06, 0x02, 0x01, 0x03, 0x02, 0x01, 0x04, 0x01]);
const txid = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

function revHex(text) {
  let out = "";
  for (let i = text.length; i > 0; i -= 2) out += text.slice(i - 2, i);
  return out;
}
function u32le(n) {
  return [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255]
    .map((b) => b.toString(16).padStart(2, "0")).join("");
}
function u64le(n) {
  let out = "";
  let x = n;
  for (let i = 0; i < 8; i++) {
    out += Number(x & 0xffn).toString(16).padStart(2, "0");
    x >>= 8n;
  }
  return out;
}
function pushHex(script) {
  return script.length.toString(16).padStart(2, "0") + hexOf(script);
}
function p2pkh(hash) {
  const out = new Uint8Array(25);
  out[0] = 0x76; out[1] = 0xa9; out[2] = 20;
  out.set(hash, 3);
  out[23] = 0x88; out[24] = 0xac;
  return out;
}
function p2sh(hash) {
  const out = new Uint8Array(23);
  out[0] = 0xa9; out[1] = 0x14;
  out.set(hash, 2);
  out[22] = 0x87;
  return out;
}
function expectTx(inputs, sigs, pay, change, typeByte, hash) {
  let out = "01000000" + inputs.length.toString(16).padStart(2, "0");
  inputs.forEach((inp, i) => {
    const script = new Uint8Array(sigs[i].length + pub.length + 2);
    script[0] = sigs[i].length;
    script.set(sigs[i], 1);
    script[1 + sigs[i].length] = pub.length;
    script.set(pub, 2 + sigs[i].length);
    out += revHex(inp.txid) + u32le(inp.vout) + pushHex(script) + "ffffffff";
  });
  const withChange = change >= 1_000_000n;
  out += withChange ? "02" : "01";
  out += u64le(pay) + pushHex(typeByte === 1 ? p2sh(hash) : p2pkh(hash));
  if (withChange) out += u64le(change) + pushHex(p2pkh(wallet160));
  return out + "00000000";
}

const one = [{ txid, vout: 1, value: 200_000_000n }];
const pay = 100_000_000n;
const change = 50_000_000n;
const blob = new Uint8Array(33 + sigA.length);
blob.set(pub, 0);
blob.set(sigA, 33);
const built = await buildSignedTx({
  inputs: one, pay, change, typeByte: 0, hash160: dest160, walletHash160: wallet160,
}, blob);
assert.equal(built, expectTx(one, [sigA], pay, change, 0, dest160));
assert.equal(built.slice(0, 8), "01000000");
assert.ok(built.endsWith("00000000"));

const dustless = await buildSignedTx({
  inputs: one, pay, change: 999_999n, typeByte: 0, hash160: dest160, walletHash160: wallet160,
}, blob);
assert.equal(dustless, expectTx(one, [sigA], pay, 999_999n, 0, dest160));
assert.equal(dustless.length < built.length, true);

const exactDust = await buildSignedTx({
  inputs: one, pay, change: 1_000_000n, typeByte: 1, hash160: dest160, walletHash160: wallet160,
}, blob);
assert.equal(exactDust, expectTx(one, [sigA], pay, 1_000_000n, 1, dest160));

const twoIn = [
  { txid, vout: 1, value: 200_000_000n },
  { txid: "aa".repeat(32), vout: 7, value: 200_000_000n },
];
const twoBlob = new Uint8Array(33 + sigA.length + sigB.length);
twoBlob.set(pub, 0);
twoBlob.set(sigA, 33);
twoBlob.set(sigB, 33 + sigA.length);
const twoBuilt = await buildSignedTx({
  inputs: twoIn, pay, change, typeByte: 0, hash160: dest160, walletHash160: wallet160,
}, twoBlob);
assert.equal(twoBuilt, expectTx(twoIn, [sigA, sigB], pay, change, 0, dest160));

const wrongKey = wallet160.slice();
wrongKey[0] ^= 1;
await assert.rejects(() => buildSignedTx({
  inputs: one, pay, change, typeByte: 0, hash160: dest160, walletHash160: wrongKey,
}, blob), /not this Game Boy/);
await assert.rejects(() => buildSignedTx({
  inputs: twoIn, pay, change, typeByte: 0, hash160: dest160, walletHash160: wallet160,
}, blob), /do not match/);

const long = new Uint8Array(60);
long[0] = 2;
long.set(sigA, 51);
const page0 = new Uint8Array(53);
page0[0] = 0;
page0[1] = 2;
page0.set(long.slice(0, 51), 2);
const page1 = new Uint8Array(2 + 9);
page1[0] = 1;
page1[1] = 2;
page1.set(long.slice(51), 2);
assert.equal(hexOf(assembleSigBlob([parseSigPage(page1), parseSigPage(page0)])), hexOf(long));
assert.throws(() => parseSigPage(page0.slice(0, 52)), /not a signed page/);
assert.throws(() => parseSigPage(Uint8Array.from([0, 9, 1, 2, 3])), /not a signed page/);

const hole = page0.slice();
hole[4] = 0xab;
const joined = assembleSigBlob([parseSigPage(hole), parseSigPage(page1)]);
assert.equal(joined[2], 0xab);

console.log("web codec ok", encoded.typedCount, "chars", encoded.groups.length, "groups");
