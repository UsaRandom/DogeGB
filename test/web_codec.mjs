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
  wireEncode,
  wireDecode,
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

const B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
function namedCount(groups) {
  return B58.indexOf(groups[0][0]) * 58 + B58.indexOf(groups[0][1]);
}

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
assert.equal(encoded.payload[0], 1);
assert.equal(encoded.hashBytes, 5);
assert.equal(encoded.payload.length, 73);
assert.equal(back.pay, payment.pay);
assert.equal(back.change, payment.change);
assert.equal(namedCount(encoded.groups), encoded.groups.length);

const chars = encoded.groups[0].split("");
assert.notEqual(chars[0], chars[1]);
const swap = chars.slice();
const tmp = swap[0];
swap[0] = swap[1];
swap[1] = tmp;
const swapped = swap.join("") + encoded.groups.slice(1).join("");
await assert.rejects(decodeString(swapped), /String length is wrong/);

const bad = chars.slice();
const nextCh = bad[0] === "2" ? "3" : "2";
bad[0] = nextCh;
const substituted = bad.join("") + encoded.groups.slice(1).join("");
await assert.rejects(decodeString(substituted), /Group 1 does not check/);

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
  assert.equal(enc.payload[0], 1);
  assert.equal(enc.hashBytes, 5);
  assert.equal(namedCount(enc.groups), enc.groups.length);
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

function fromHex(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  return out;
}
function shiftPair(group) {
  const chars = group.split("");
  const i0 = B58.indexOf(chars[0]);
  const i1 = B58.indexOf(chars[1]);
  if (i0 < 57 && i1 > 0) {
    chars[0] = B58[i0 + 1];
    chars[1] = B58[i1 - 1];
  } else {
    chars[0] = B58[i0 - 1];
    chars[1] = B58[i1 + 1];
  }
  return chars.join("");
}

const moved = encoded.groups.slice();
moved[moved.length - 1] = shiftPair(moved[moved.length - 1]);
await assert.rejects(decodeString(moved.join("")), /end check does not match/);

await assert.rejects(wireEncode(Uint8Array.of(0, 1, 2)), /Version byte is zero/);
const big = new Uint8Array(300);
big[0] = 1;
big.fill(2, 1);
await assert.rejects(wireEncode(big), /wrong length/);

const wireVectors = [
  ["030faeab7cad092b33f79ee571edaf6bc756e95f749304fefed025820cdaef2584b3100bedb47e2a67552727800804070864f69b704d9405666c1f7a6c4c448dc81fed75ca7bf8df57ec2524829cca7999c510c378467921028f8951c841e7d7646ce2ff034365be1f",
    "1P114SJA TQX1q7dv Xedziw3S QKcokA11 NwVuk6sN A91RthRh GUgXTJM1 9mXE6D4z 7rf9fT2s bhyRkRaP uPDYuaqK xzZCz6cM zoCqdBGr 3qe3fFPp 5bD95r72 YriDimrc Te8Ei9AS zyyP6vi1 n1AJwrcc WLLQFfFh ywNSRtzy L7sQVCm8"],
  ["03af707f9aba591f6621e4aa57f0383766a85601bf35d8fa661ecd49bed0b75a0cedbe87900bae70c586b94f86069630a18ac0ef70a398cc6c132a2e46e2958ba51320d77d6e2f9c695f153af7ac13374836067b300b3b362c21cb1c32ba586bcda02364ec656d2b98",
    "1P1158vV uK5S21wf jgbaZodX JTz8t8H8 dGBuQK3h DjWY1thY fQopapac c1MXxe58 Wog6S2tP mGf5BuF4 nYRuSDKa ihMysnHd i71gY2kn AWvLUPP8 fRzVGcxM QDyt4oCV UgULsYsC iNwBHzf6 kPyfkYAB zKMAEMRn CLyXkMuz QA5ucnSL"],
  ["036e74d7c99d8fbf89a5bc21d8c834e05dddf96ed4182cdb49572103dd4b6afd8c5ec47e0e1ff3f3be9bff30556d801ecf1ddcfbd743a2c767dc91222bfca6379593823e3a0ccd3f64cd05617b236662bffb4865850b1c9daf3686d6841f6a3cf7a8697801a56beb7c",
    "1P114rQg PejLNXJF PV4BifXy vxoxUMPo pt7KpxF7 QLNe6H9E MXaHma35 a8yiwrfo qh8AFhud KA9Z9sr1 UbwfdwMY Dq66JhHU iH4A88Jj ik23xYdc Db8FtoWL qbD4b9j8 nD4N2ZEC Jwa5PArF GXeu2tKX VCtktgqg KfR4YNfz APvGu1ZA"],
  ["01018bc344594d725eecb01b311789b5e5fb9a7e034ee6601dc73c9010e1580b722022881b97f7c5dda8a59f5fece1844cf32d430072aceba6e62b10e1ac171cfabb2481b7126dd38a",
    "1H11114L oV4qpVSt pPMxtpCN PBDgaNrD efB1TLs7 92ukeuYs DJHEHoLQ XxcwqV9T mEnVxaG2 oeQUarJ1 wZ4M3s6s rsVFj7br baSLqgQp mbJLHCtK X68g5i9K PEkEwpbu"],
  ["0102d7c6d3f1062031732f47273303f8cebd8a5f4f141e7e19c69de4429d4e0216bf0fa2a7f8288c289c9f28e00c31354b7b8a030728c41b07f443b8928a87b36ed31971dd5e50a2810f4621a2618c38b2930059399100af3b7b7a9bda5bf7e51837333ca3957b0c401b6c",
    "1PP7RE5a rFNxV11t kXNgnzGK 4VmqRaKR e7VxBmek KHQeZsfh qZER3tmh E8B5t78h ZxibUmyz MmFyLPFG DkM3M7gT wKxCK3J2 uW52pw2F 4XrEYcse ktxR7hyk feMG6yUR yQqQq3Fh QpTwKZ6Y qU4oRy9f z7FasBTP 7ZiedVms 1rTxwRdD"],
  ["01048bad3702df3a1092312905d0d7b81b12baa421848bc3747cb61eee19197aac5d2ed9fea43d2a65220b96fa81d63eda9ada3233f137d4d3ea7075ca0f0d6b1c6b84b2c1276922ba8d343190b028db577bed14afc82cbd95982a28631c35e269d4d4bf74bcb5afbfae06171df283b0ba21095cd9311da7a79f241c99f012f0452a00422abee1644da81173c9177e88ad43a264c6e50592cdc600c1428baafad260147c31189ff2238264ba63645b",
    "1d11111d DrQPXdXV AsW6cfyq sKeu3RWf Nb4cZA3M dcYLTeXh p3CkXjst 18272yVi 4KNJqy7v 1Q2GGwbT WE6ryHKD maVpeBa1 h4sfKqAZ g27rCznb rn1v75we QsuF1wxH ewcY1ZJZ VFm44NbY E1fjv1uR t5PTDsfW f5piNNig sQV7ZPug MGjBFbbv Fj8QEPQV cZpNGbkv SMbphA34 SajLmxXH vVQdPVgx fdxXZLJv Y1M3BgL6 8XMnTffX m6bSpvxY jmuA1srF RorJ6oKY USuavYSF MD23oyg3"],
  ["01010c95e62842b5a9cb559b1dc2885c4f5d1a4f17d703ce558a8036891113b59df70a776666681dc549cfc2851d8920804584ae00b4155e92fff123901b58153f75bd0586b2462397",
    "1H11114L o4Q9RZn8 LfFM2Coa 2UVzAWbC 9ys8bzR5 NQ1rWXJv CeuGjQou ensEN8ji tJQcAB6b xoTgJ6Ui y9GxP5J4 sZzHr1b7 XxyrZV3M kJjtFKw8 v2LcT2n7 S3c3axyb"],
  ["0103ebda7b94855cc1ada8eee085bc3e14098391b78e39644f9735f99a4ccd3462af835b7bea75597281afcbe563662c9add117147ca0b42bfce587eb6d6b2b87fc87e949ab0a51fc631dbcf18f6d13cce7f6d0b189651470ea3ba01b62e742f61be1750d0465fd2ac4e9b36cebf0b8437c78b23702115200026a60fa19e60efc12e437eb735b453dc5d29d20f",
    "1W113DVE aGMcuXDQ LBq2TTxC jBgDWCMp kUSoEh3P 6Lf5dzBv 9JGSYtKr 8kdGzyS8 yE6Dicvi wTnhWsTf npEnjzSj sGRq93pM nNPPH1bm f8Ra9shT GF94QzZd ER1SnfPt 7cxp4kKa megRJrvY BTbXB395 L8QddGJe NM4mSmWD cfYAhqoG Q31PkAaH 6FXn4SPV C43SzJrq Hmk1UpRU BwRUsF6B JhuvcXGB 7BSroQxh"],
  ["0102bcb78a967818c46e63a50dd56024a00a1b40b79d3dd9d4ef52113b6210bd895abb4d9c0efdac9594a6480b40674c632e98a76e409fda6b448784379a0de17320e1c159fb05c036237fc9e484d727522aa36d36900089ec0fab392cbc13685dd0e67a1e8a034077d5da",
    "1PP6trPx 8BaYHppJ ehPUW9SF KCXrp1XC TQfSGTqU 78uVTsUP uFvjAWTt f7Zh2gFw EiR19WJH 1sQ8wXod 19sGYi6b aGvPXAsf MYiLFx4A gaqDCqm4 dJ1341hh pg8zKfY6 WAZJUjT9 XD67Asso bwkxoaS1 vAHhemwN KxzyWxZD AcppNXQf"],
  ["01014b7860f60053f464ad5b5dabe7ffbf2fbf14475161fd25379059edd4b100f040882e52452d3a696351dd6876692f9b7b9f1f0062dc69c1432b573747553ec5fe4d1ddd0bb8388e",
    "1H11114L oGbaKwnE v3zwxgBf nyYKwMTK w1D4AVLA ryB5QEXC 6yHwZ7qk dc6SHENc Q858asPX vSVrkcvw cU34Y26o Sz1USBJn FaNpn9eY YyeVXVeG 2tC2CM3g 5g7KdKWb"],
  ["01022163038c9ee04a5539c717bec0bcdddd77e21c49bd5b5ab42141836e26007f937e310a46303ea5bce4d1253ffb15174206081dcc000bff740f64e3ce9545bf821ab94b17b486b6eb16b82bea62ae8ce861b6104f00469e90cff3e0fae092fb6e7375b53e42729943fa",
    "1PP3tVjt B8YxaXjb mJYnQ3RD hRSdJUzu vMhpzjiB TrE4Ke5b zcj7WfMv v2gnUGx4 YXHtaE99 Nzo1hNkw Zm7JvNSR uJdpCsN3 2KGnhxmm 62DSYVfR pNTt5gr6 G9mFiJzP 5v8xuKZp KAfGusUc BUK1RKL1 SrRokm32 j59EyGm9 MF1hcgZ7"],
  ["0103f2e1c0631c745cde4d427a8356af97d3533994125e1943e43fb474bf88db7ba493a8f4b6601b82f655298ca46648134c613cc1202873a842acfda4f697cd17f5e0f62bd576322eb904757ada0892123a1fd48048591abde5c40f4673ba911a42ea750df65a75c05b850a774fa31beea3a20e756cb3f500c04d17ed6548425319576124f7f8df071cd17cbf",
    "1W113DWF MvUAfa5B 7hvzjgej eUrMG8P4 qcu9mHnH CsgSDXSK vtyFu31w QDyFkUxy 4j7BNhHP xkSZbEGk 4X4wGL7F U2wm8wFU vmdTkuKh 1d8BqnkG aGWqhoDr Ua7mB5FP ba9Dw8Y6 dxHksf39 RrBWM7h5 9ynTt6Zr eFta99vX pSimYcja W19AiGL6 4cVRJjMv MTRzzjSL ZJJQErHt 9XaEFdq9 wiV6J46e E5XNkV2R"],
  ["010203", "1311Ldpo 3ueys1AZ"],
];
for (const [hex, text] of wireVectors) {
  const bytes = fromHex(hex);
  const groups = await wireEncode(bytes);
  assert.equal(groups.join(" "), text);
  assert.equal(namedCount(groups), groups.length);
  assert.equal(hexOf(await wireDecode(text)), hex);
  const flipped = text.split(" ");
  const ch = flipped[0].split("");
  ch[3] = ch[3] === "2" ? "3" : "2";
  flipped[0] = ch.join("");
  await assert.rejects(wireDecode(flipped.join(" ")), /Group 1 does not check/);
}
const other = fromHex(wireVectors[3][0]);
other[0] = 0x02;
const otherGroups = await wireEncode(other);
assert.equal(hexOf(await wireDecode(otherGroups.join(""))), hexOf(other));
await assert.rejects(decodeString(otherGroups.join(" ")), /old app/);
await assert.rejects(decodeString("1311Ldpo 3ueys1AZ"), /String ended early|Input count|Destination type|extra bytes/);

console.log("web codec ok", encoded.typedCount, "chars", encoded.groups.length, "groups");
