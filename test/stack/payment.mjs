// Make a random test payment and print its keyboard groups as JSON.
// Usage: node payment.mjs <codec.js> <doge|sol> [inputs]
import fs from 'fs'; import crypto from 'crypto';
const [codec, mode, nin] = process.argv.slice(2);
eval(fs.readFileSync(codec, 'utf8'));
const rnd = n => new Uint8Array(crypto.randomBytes(n));
let r;
if (mode === 'sol') {
  r = await globalThis.SolCodec.encodeString({ lamports: 12345678n, dest: rnd(32), nonceAccount: rnd(32), nonce: rnd(32) });
} else {
  const inputs = []; for (let i = 0; i < Number(nin || 1); i++) inputs.push({ txid: Buffer.from(rnd(32)).toString('hex'), vout: i });
  r = await globalThis.DogeCodec.encodeString({ inputs, pay: 123456789n, change: 50000000n, typeByte: 0, hash160: rnd(20) });
}
console.log(JSON.stringify(r.groups));
