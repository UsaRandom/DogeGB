if (!globalThis.DogeCodec) throw new Error("This page did not start.");
const {
  assembleSigBlob,
  buildSignedTx,
  decodeAddress,
  dogeToKoinu,
  encodeString,
  extractPayment,
  feeRatePerByte,
  formatDoge,
  parseSigPage,
  priceFixed,
  scriptP2pkh,
  selectCoins,
} = globalThis.DogeCodec;

const screen = document.getElementById("screen");
const backBtn = document.getElementById("back");
const dotsEl = document.getElementById("dots");

const state = {
  step: "home",
  scanFor: "wallet",
  scanError: "",
  formError: "",
  wallet: null,
  dest: null,
  destText: "",
  amountText: "",
  amountFromCode: false,
  feeText: "1.0",
  coins: [],
  nextId: 1,
  lookup: "idle",
  lookupGen: 0,
  plan: null,
  result: null,
  page: 0,
  draft: "",
  sigParts: [],
  sigCount: 0,
  txHex: "",
  txHash: "",
  sendError: "",
};

let scanGen = 0;
let sendGen = 0;
let stream = null;
let cameraAsked = false;

function h(tag, props, children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props || {})) {
    if (key === "class") node.className = value;
    else if (key.startsWith("on") && typeof value === "function") {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else if (value === true) node.setAttribute(key, "");
    else if (value !== false && value != null) node.setAttribute(key, String(value));
  }
  for (const child of children || []) {
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

function friendlyDoge(koinu) {
  const raw = formatDoge(koinu);
  const neg = raw.startsWith("-");
  const body = neg ? raw.slice(1) : raw;
  const [whole, frac] = body.split(".");
  const trimmed = frac.replace(/0+$/, "");
  return (neg ? "-" : "") + (trimmed ? whole + "." + trimmed : whole);
}

function spaced(text) {
  return (text.match(/.{1,4}/g) || [text]).join(" ");
}

function spendable() {
  return state.coins.filter((c) => c.spendable);
}

function totalSpendable() {
  return spendable().reduce((n, c) => n + c.value, 0n);
}

function checkedCoins() {
  return state.coins.filter((c) => c.checked && c.spendable);
}

function stepIndex() {
  if (state.step === "pay") return 1;
  if (state.step === "review") return 2;
  if (state.step === "type" || state.step === "send" || state.step === "sent") return 3;
  if (state.step === "scan" && state.scanFor === "signed") return 3;
  return 0;
}

function stopCamera() {
  scanGen += 1;
  if (stream) {
    for (const track of stream.getTracks()) track.stop();
    stream = null;
  }
  const cam = document.getElementById("cam");
  if (cam) cam.srcObject = null;
}

function go(step) {
  state.formError = "";
  state.scanError = "";
  state.step = step;
  render();
}

function rememberPay() {
  const dest = document.getElementById("dest");
  const amount = document.getElementById("amount");
  const fee = document.getElementById("fee");
  if (dest) state.destText = dest.value;
  if (amount) state.amountText = amount.value;
  if (fee) state.feeText = fee.value;
}

function hexEq(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

function parseJsonBig(text) {
  const safe = text.replace(
    /"(value|balance|total_received|total_sent|final_balance|unconfirmed_balance)"\s*:\s*(-?\d+)/g,
    '"$1":"$2"'
  );
  return JSON.parse(safe);
}

function asBig(v) {
  if (typeof v === "bigint") return v;
  if (typeof v === "string") return BigInt(v);
  if (typeof v === "number" && Number.isSafeInteger(v)) return BigInt(v);
  throw new Error("A coin value was too large to read.");
}

async function fetchBlockcypher(address) {
  const url = "https://api.blockcypher.com/v1/doge/main/addrs/" +
    encodeURIComponent(address) + "?unspentOnly=true&includeScript=true&limit=200";
  const res = await fetch(url);
  const text = await res.text();
  if (!res.ok) throw new Error("lookup failed");
  const data = parseJsonBig(text);
  const expected = scriptP2pkh(state.wallet.hash160);
  const coins = [];
  const take = (ref, confirmed) => {
    if (!ref || ref.spent) return;
    if (ref.tx_input_n !== undefined && ref.tx_input_n !== -1) return;
    const scriptHex = (ref.script || "").toLowerCase();
    let ok = true;
    if (scriptHex) {
      const got = new Uint8Array(scriptHex.match(/../g).map((b) => parseInt(b, 16)));
      ok = hexEq(got, expected);
    }
    coins.push({
      id: state.nextId++,
      txid: String(ref.tx_hash).toLowerCase(),
      vout: ref.tx_output_n,
      value: asBig(ref.value),
      confirmations: confirmed ? Number(ref.confirmations || 0) : 0,
      spendable: ok,
      checked: false,
    });
  };
  for (const ref of data.txrefs || []) take(ref, true);
  for (const ref of data.unconfirmed_txrefs || []) take(ref, false);
  return coins;
}

async function fetchChainz(address) {
  const url = "https://chainz.cryptoid.info/doge/api.dws?q=unspent&active=" +
    encodeURIComponent(address);
  const res = await fetch(url);
  const text = await res.text();
  if (!res.ok) throw new Error("lookup failed");
  const data = parseJsonBig(text);
  if (data && data.error) throw new Error("lookup failed");
  const list = Array.isArray(data) ? data : (data.unspent_outputs || []);
  return list.map((u) => ({
    id: state.nextId++,
    txid: String(u.tx_hash || u.txid || u.tx_hash_big_endian).toLowerCase(),
    vout: Number(u.tx_output_n ?? u.vout ?? u.n),
    value: asBig(u.value),
    confirmations: Number(u.confirmations || 0),
    spendable: true,
    checked: false,
  }));
}

async function lookup() {
  const gen = ++state.lookupGen;
  state.lookup = "loading";
  state.coins = [];
  if (state.step === "found" || state.step === "pay") render();
  let coins = null;
  try {
    coins = await fetchBlockcypher(state.wallet.text);
  } catch { /* try the next lookup */ }
  if (!coins) {
    try { coins = await fetchChainz(state.wallet.text); }
    catch { /* leave coins empty */ }
  }
  if (gen !== state.lookupGen) return;
  if (!coins) state.lookup = "error";
  else {
    state.coins = coins;
    state.lookup = "done";
  }
  if (state.step === "found" || state.step === "pay") {
    rememberPay();
    render();
  }
}

async function readCode(raw, typed) {
  if (!String(raw).trim()) {
    return { error: typed ? "Type the whole address." : "That code is not a Dogecoin address." };
  }
  let parsed;
  try { parsed = extractPayment(raw); }
  catch { return { error: typed ? "That address does not look right." : "That code is not a Dogecoin address." }; }
  try {
    const decoded = await decodeAddress(parsed.address);
    return { decoded, amount: parsed.amount };
  } catch {
    return {
      error: typed
        ? "That address does not look right. Check it and try again."
        : "That did not read cleanly. Hold steady and try again.",
    };
  }
}

async function useWallet(raw, typed) {
  const found = await readCode(raw, typed);
  if (found.error) return found.error;
  if (found.decoded.kind !== "p2pkh") return "The Game Boy address should start with D.";
  state.wallet = found.decoded;
  state.coins = [];
  state.lookup = "loading";
  state.step = "found";
  state.formError = "";
  render();
  lookup();
  return "";
}

async function useDest(raw) {
  const found = await readCode(raw);
  if (found.error) return found.error;
  state.dest = found.decoded;
  state.destText = found.decoded.text;
  if (found.amount && !state.amountText) {
    try {
      dogeToKoinu(found.amount);
      state.amountText = found.amount;
      state.amountFromCode = true;
    } catch { /* ignore a bad amount on the code */ }
  }
  state.step = "pay";
  state.formError = "";
  render();
  return "";
}

function typePages() {
  const pages = [];
  const groups = state.result.groups;
  for (let i = 0; i < groups.length; i += 2) {
    const items = [{ label: String(i + 1).padStart(2, "0"), text: groups[i] }];
    if (groups[i + 1]) items.push({ label: String(i + 2).padStart(2, "0"), text: groups[i + 1] });
    pages.push(items);
  }
  return pages;
}

function primary(label, onClick, disabled) {
  return h("button", { class: "primary", type: "button", onclick: onClick, disabled: !!disabled }, [label]);
}

function secondary(label, onClick) {
  return h("button", { class: "secondary", type: "button", onclick: onClick }, [label]);
}

function textButton(label, onClick) {
  return h("button", { class: "text", type: "button", onclick: onClick }, [label]);
}

function wide() {
  return window.matchMedia("(min-width: 800px)").matches;
}

function walletForm() {
  const input = h("input", {
    id: "wallet-draft",
    type: "text",
    value: state.draft,
    autocomplete: "off",
    spellcheck: "false",
    autocapitalize: "off",
    placeholder: "Starts with D",
  });
  input.addEventListener("input", () => { state.draft = input.value; });
  return h("form", { class: "stack", onsubmit: async (e) => {
    e.preventDefault();
    state.draft = input.value;
    const err = await useWallet(input.value, true);
    if (err) {
      state.formError = err;
      render();
    }
  } }, [
    h("label", {}, ["Address on the Game Boy", input]),
    h("p", { class: "bad" }, [state.formError]),
    h("button", { class: "primary", type: "submit" }, ["Look it up"]),
  ]);
}

function renderHome() {
  if (wide()) {
    screen.append(
      h("h1", {}, ["Send Dogecoin"]),
      h("p", { class: "lead" }, ["On your Game Boy, open the address code."]),
      h("p", { class: "lead" }, ["Type it here. Your words and your key stay on the Game Boy."]),
      walletForm(),
      h("div", { class: "actions" }, [
        secondary("Scan the code", () => startScan("wallet")),
      ])
    );
    return;
  }
  screen.append(
    h("h1", {}, ["Send Dogecoin"]),
    h("p", { class: "lead" }, ["On your Game Boy, open the address code."]),
    h("p", { class: "lead" }, ["Then scan it with this phone. Your words and your key stay on the Game Boy."]),
    h("div", { class: "actions" }, [
      primary("Scan the code", () => startScan("wallet")),
      textButton("Type the address", () => go("typeWallet")),
    ])
  );
}

function renderTypeWallet() {
  screen.append(
    h("h1", {}, ["Type your address"]),
    h("p", { class: "lead" }, ["The code is on the Game Boy address screen. Type it here if the camera cannot see it."]),
    walletForm(),
    textButton("Scan instead", () => startScan("wallet"))
  );
}

function renderFound() {
  const total = totalSpendable();
  const bits = [
    h("h1", {}, ["Your Game Boy"]),
    h("p", { class: "addr" }, [spaced(state.wallet.text)]),
    h("p", { class: "lead" }, ["Anything left over comes back to this wallet."]),
  ];
  if (state.lookup === "loading") {
    bits.push(h("p", { class: "money" }, ["Looking up your Dogecoin…"]));
  } else if (state.lookup === "error") {
    bits.push(h("p", { class: "bad" }, ["The lookup did not answer. Check your connection."]));
    bits.push(primary("Try again", () => lookup()));
    bits.push(addCoinForm());
  } else if (total === 0n) {
    bits.push(h("p", { class: "money" }, ["There is no Dogecoin here yet."]));
    bits.push(h("p", { class: "lead" }, ["Receive some on the Game Boy, then scan again."]));
    bits.push(primary("Scan a different wallet", () => startScan("wallet")));
    bits.push(addCoinForm());
  } else {
    bits.push(h("p", { class: "money" }, ["You have " + friendlyDoge(total) + " DOGE."]));
    bits.push(h("div", { class: "actions" }, [
      primary("Continue", () => go("pay")),
    ]));
  }
  screen.append(...bits);
}

function addCoinForm() {
  const txid = h("input", { id: "hand-txid", type: "text", autocomplete: "off", spellcheck: "false", autocapitalize: "off" });
  const vout = h("input", { id: "hand-vout", type: "text", inputmode: "numeric", autocomplete: "off" });
  const amount = h("input", { id: "hand-amount", type: "text", inputmode: "decimal", autocomplete: "off" });
  const err = h("p", { class: "bad", id: "hand-error" }, []);
  const form = h("form", { class: "stack", onsubmit: (e) => {
    e.preventDefault();
    err.textContent = "";
    try {
      const id = txid.value.trim().toLowerCase();
      if (!/^[0-9a-f]{64}$/.test(id)) throw new Error("The transaction code is 64 letters and numbers.");
      const n = Number(vout.value);
      if (!Number.isInteger(n) || n < 0 || n > 65535) throw new Error("The output number looks wrong.");
      const value = dogeToKoinu(amount.value);
      if (value <= 0n) throw new Error("Enter how much that coin is.");
      if (state.coins.some((c) => c.txid === id && c.vout === n)) throw new Error("That coin is already listed.");
      state.coins.push({
        id: state.nextId++, txid: id, vout: n, value,
        confirmations: 1, spendable: true, checked: false,
      });
      if (state.lookup !== "done") state.lookup = "done";
      render();
    } catch (problem) {
      err.textContent = problem.message;
    }
  } }, [
    h("label", {}, ["Transaction code", txid]),
    h("label", {}, ["Output number", vout]),
    h("label", {}, ["How much is that coin?", amount]),
    err,
    h("button", { class: "secondary", type: "submit" }, ["Add this coin"]),
  ]);
  return h("details", {}, [
    h("summary", {}, ["The lookup missed a coin"]),
    form,
  ]);
}

function renderPay() {
  const dest = h("input", {
    id: "dest", type: "text", value: state.destText,
    autocomplete: "off", spellcheck: "false", autocapitalize: "off",
    placeholder: "Their address",
  });
  const amount = h("input", {
    id: "amount", type: "text", inputmode: "decimal", value: state.amountText,
    autocomplete: "off",
  });
  const fee = h("input", {
    id: "fee", type: "text", inputmode: "decimal", value: state.feeText, autocomplete: "off",
  });
  const bits = [
    h("h1", {}, ["Who are you paying?"]),
  ];
  if (state.lookup === "loading") bits.push(h("p", { class: "note" }, ["Looking up your Dogecoin…"]));
  else if (state.lookup === "done") {
    const total = totalSpendable();
    bits.push(h("p", { class: "money" }, [
      total === 0n ? "There is no Dogecoin to send yet." : "You have " + friendlyDoge(total) + " DOGE.",
    ]));
  } else if (state.lookup === "error") {
    bits.push(h("p", { class: "bad" }, ["The lookup did not answer."]));
  }
  if (state.amountFromCode) bits.push(h("p", { class: "note" }, ["The amount was filled in from their code."]));
  bits.push(secondary("Scan their code", () => { rememberPay(); startScan("dest"); }));
  bits.push(h("div", { class: "pay-fields" }, [
    h("label", {}, ["Their address", dest]),
    h("label", { class: "amount" }, ["How much?", amount]),
  ]));
  bits.push(h("p", { class: "bad" }, [state.formError]));
  bits.push(h("div", { class: "actions" }, [
    primary("Next", () => submitPay()),
  ]));
  bits.push(advanced(fee));
  screen.append(...bits);
}

function advanced(feeInput) {
  const list = h("ul", { class: "coins" }, spendable().map((coin) => {
    const box = h("input", { type: "checkbox", checked: coin.checked });
    box.addEventListener("change", () => { coin.checked = box.checked; });
    const waiting = coin.confirmations === 0 ? " · waiting" : "";
    return h("li", {}, [
      box,
      h("div", {}, [
        friendlyDoge(coin.value) + " DOGE",
        h("span", {}, [coin.txid.slice(-4) + waiting]),
      ]),
    ]);
  }));
  return h("details", {}, [
    h("summary", {}, ["More options"]),
    h("p", { class: "note" }, ["Leave this alone unless a payment will not go through."]),
    h("label", {}, ["Network fee, DOGE per 1000 bytes", feeInput]),
    h("p", { class: "note" }, ["Tick coins to choose them yourself. Leave them unticked and we choose."]),
    list,
    addCoinForm(),
  ]);
}

async function submitPay() {
  rememberPay();
  state.formError = "";
  if (state.lookup === "loading") {
    state.formError = "Still looking up your Dogecoin.";
    render();
    return;
  }
  if (state.lookup === "error" && spendable().length === 0) {
    state.formError = "The lookup did not answer, so there are no coins to send.";
    render();
    return;
  }
  let dest;
  try { dest = await decodeAddress(state.destText); }
  catch {
    state.formError = "Check their address and try again.";
    render();
    return;
  }
  let pay;
  try { pay = dogeToKoinu(state.amountText); }
  catch {
    state.formError = "Enter the amount in Dogecoin.";
    render();
    return;
  }
  if (pay <= 0n) {
    state.formError = "Enter an amount greater than zero.";
    render();
    return;
  }
  let rate;
  try { rate = feeRatePerByte(state.feeText || "1.0"); }
  catch {
    state.formError = "The fee number looks wrong.";
    render();
    return;
  }
  const chosen = checkedCoins();
  let plan;
  let inputs;
  try {
    if (chosen.length === 0) {
      if (spendable().length === 0) throw new Error("This wallet has no Dogecoin to send.");
      const picked = selectCoins(spendable(), pay, rate, dest.kind === "p2sh");
      plan = picked;
      inputs = picked.chosen;
    } else {
      if (chosen.length > 4) throw new Error("Pick at most 4 coins.");
      const total = chosen.reduce((n, c) => n + c.value, 0n);
      plan = priceFixed(chosen.length, total, pay, rate, dest.kind === "p2sh");
      if (!plan) throw new Error("Those coins do not cover the payment and the fee.");
      inputs = chosen;
    }
  } catch (problem) {
    state.formError = plainPayError(problem.message, pay);
    render();
    return;
  }
  state.dest = dest;
  state.plan = { ...plan, pay, inputs };
  try {
    state.result = await encodeString({
      inputs: inputs.map((c) => ({ txid: c.txid, vout: c.vout, value: c.value })),
      pay, fee: plan.fee, change: plan.change,
      typeByte: dest.typeByte, hash160: dest.hash160,
    });
  } catch (problem) {
    state.formError = problem.message;
    render();
    return;
  }
  state.page = 0;
  state.sigParts = [];
  state.sigCount = 0;
  state.txHex = "";
  state.txHash = "";
  state.sendError = "";
  state.step = "review";
  render();
}

function plainPayError(message, pay) {
  if (message.includes("no Dogecoin")) return "This wallet has no Dogecoin to send.";
  if (message.includes("first 4") || message.includes("at most 4")) {
    return "That amount is split across too many coins. Try a smaller amount.";
  }
  if (message.includes("do not cover")) {
    const have = totalSpendable();
    if (have <= pay) return "You have " + friendlyDoge(have) + " DOGE. That is not enough.";
    return "Keep a little back for the fee, or send a smaller amount.";
  }
  return message;
}

function renderReview() {
  const plan = state.plan;
  const same = state.dest.text === state.wallet.text;
  const lines = [
    h("h1", {}, ["Check this"]),
    h("p", { class: "money" }, ["Send " + friendlyDoge(plan.pay) + " DOGE"]),
    h("p", { class: "addr" }, [spaced(state.dest.text)]),
    h("p", { class: "lead" }, ["The network fee is " + friendlyDoge(plan.fee) + " DOGE."]),
    h("p", { class: "lead" }, [
      plan.change === 0n
        ? "Nothing comes back. The rest goes to the fee."
        : friendlyDoge(plan.change) + " DOGE comes back to your Game Boy.",
    ]),
  ];
  if (same) lines.push(h("p", { class: "note" }, ["This pays your own Game Boy. Only the fee leaves."]));
  lines.push(h("div", { class: "actions" }, [
    primary("Show what to type", () => go("type")),
    textButton("Change the amount", () => go("pay")),
  ]));
  screen.append(...lines);
}

function renderType() {
  const rows = typePages().map((page) => h("div", { class: "type-row" }, page.map((item) => (
    h("div", { class: "group-block" }, [
      h("p", { class: "num" }, [item.label]),
      h("p", { class: "chars" }, [item.text]),
    ])
  ))));
  screen.append(
    h("h1", {}, ["Type this"]),
    h("p", { class: "lead" }, ["Type every character. Skip the small number."]),
    h("div", { class: "type-sheet" }, [
      ...rows,
      h("details", {}, [
        h("summary", {}, ["Save a copy"]),
        textButton("Copy everything", copyAll),
      ]),
    ]),
    h("div", { class: "actions" }, [
      primary("Scan the signed codes", startSignedScan),
      textButton("Done", finishPay),
    ])
  );
}

async function copyAll(event) {
  const lines = [];
  for (const page of typePages()) lines.push(page.map((item) => item.text).join("  "));
  const btn = event.currentTarget;
  try {
    await navigator.clipboard.writeText(lines.join("\n"));
    btn.textContent = "Copied";
  } catch {
    btn.textContent = "Copy failed";
  }
}

function cameraAvailable() {
  return !!(navigator.mediaDevices && typeof navigator.mediaDevices.getUserMedia === "function");
}

function iphone() {
  return /iPhone|iPad|iPod/.test(navigator.userAgent) ||
    (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
}

function stopTracks(media) {
  if (!media) return;
  for (const track of media.getTracks()) track.stop();
}

function prepareCamera(video) {
  video.muted = true;
  video.defaultMuted = true;
  video.autoplay = true;
  video.playsInline = true;
  video.setAttribute("muted", "");
  video.setAttribute("autoplay", "");
  video.setAttribute("playsinline", "true");
  video.setAttribute("webkit-playsinline", "true");
}

function placeCamera() {
  const video = document.getElementById("cam");
  const slot = document.querySelector(".finder");
  if (!video || !slot || state.step !== "scan") return;
  const box = slot.getBoundingClientRect();
  const vv = window.visualViewport;
  const offsetLeft = vv ? vv.offsetLeft : 0;
  const offsetTop = vv ? vv.offsetTop : 0;
  const edge = 3;
  video.style.left = (box.left + offsetLeft + edge) + "px";
  video.style.top = (box.top + offsetTop + edge) + "px";
  video.style.width = Math.max(1, box.width - edge * 2) + "px";
  video.style.height = Math.max(1, box.height - edge * 2) + "px";
  video.style.transform = "translateZ(0)";
  video.style.webkitTransform = "translateZ(0)";
  const frame = slot.querySelector(".frame");
  if (!frame) return;
  frame.style.position = "fixed";
  frame.style.zIndex = "3";
  frame.style.inset = "auto";
  frame.style.left = (box.left + offsetLeft + box.width * 0.12) + "px";
  frame.style.top = (box.top + offsetTop + box.height * 0.14) + "px";
  frame.style.width = (box.width * 0.76) + "px";
  frame.style.height = (box.height * 0.72) + "px";
}

function parkCamera() {
  const video = document.getElementById("cam");
  if (!video) return;
  video.style.left = "";
  video.style.top = "";
  video.style.width = "";
  video.style.height = "";
  video.style.transform = "";
  video.style.webkitTransform = "";
}

function renderScan() {
  const live = cameraAvailable();
  const video = document.getElementById("cam");
  if (video) prepareCamera(video);
  const photo = h("input", { id: "photo", type: "file", accept: "image/*", capture: "environment" });
  photo.addEventListener("change", () => {
    const file = photo.files && photo.files[0];
    photo.value = "";
    readPhoto(file);
  });
  const signed = state.scanFor === "signed";
  const title = signed ? "Scan the signed codes" : (state.scanFor === "wallet" ? "Scan your Game Boy" : "Scan their code");
  const bits = [
    h("h1", {}, [title]),
    h("p", { class: "lead" }, [signed
      ? signedProgress()
      : (live
        ? "Tap Allow the camera. Safari will ask before the picture starts."
        : "Take a photo of the code.")]),
  ];
  if (live) bits.push(h("div", { class: "finder" }, [h("div", { class: "frame" })]));
  bits.push(h("p", { class: "bad" }, [state.scanError]));
  if (live) {
    const allow = primary("Allow the camera", askCamera);
    allow.id = "allow-cam";
    bits.push(allow);
  }
  bits.push(h("label", {
    id: "take-photo",
    class: live ? "secondary pick" : "primary pick",
  }, [live ? "Take a photo instead" : "Take a photo", photo]));
  if (!signed) {
    bits.push(textButton("Type it instead", () => go(state.scanFor === "wallet" ? "typeWallet" : "pay")));
  }
  screen.append(...bits);
  if (live) placeCamera();
}

function askCamera() {
  if (cameraAsked) return;
  const video = document.getElementById("cam");
  const button = document.getElementById("allow-cam");
  const gen = scanGen;
  if (!video || !cameraAvailable()) {
    failCamera(gen, { name: "NotSupportedError" });
    return;
  }
  cameraAsked = true;
  if (button) button.disabled = true;
  prepareCamera(video);
  placeCamera();
  const prime = video.play();
  if (prime && prime.catch) prime.catch(() => {});
  const bad = screen.querySelector(".bad");
  if (bad) bad.textContent = "Asking for the camera…";
  requestAnimationFrame(placeCamera);
  let request;
  try {
    request = navigator.mediaDevices.getUserMedia({
      audio: false,
      video: { facingMode: { ideal: "environment" } },
    });
  } catch (err) {
    cameraAsked = false;
    if (button) button.disabled = false;
    failCamera(gen, err);
    return;
  }
  request.then((got) => {
    gotCamera(gen, got);
  }).catch((err) => {
    cameraAsked = false;
    if (button) button.disabled = false;
    failCamera(gen, err);
  });
}

function failCamera(gen, problem) {
  if (gen !== scanGen || state.step !== "scan") return;
  cameraAsked = false;
  if (stream) {
    stopTracks(stream);
    stream = null;
  }
  const video = document.getElementById("cam");
  if (video) video.srcObject = null;
  scanGen += 1;
  const name = (problem && problem.name) || "Error";
  if (name === "NotAllowedError") {
    state.scanError = iphone()
      ? "Safari blocked the camera. Tap aA in the address bar, open Website Settings, and allow Camera."
      : "The browser blocked the camera. Allow it for this site, then tap Allow the camera.";
  } else {
    state.scanError = "The live camera did not start (" + name + "). Take a photo instead.";
  }
  const bad = screen.querySelector(".bad");
  if (bad) bad.textContent = state.scanError;
  const button = document.getElementById("allow-cam");
  if (button) {
    button.hidden = false;
    button.disabled = false;
  }
  const photoBtn = document.getElementById("take-photo");
  if (photoBtn) photoBtn.className = "primary pick";
  requestAnimationFrame(placeCamera);
}

function gotCamera(gen, got) {
  if (gen !== scanGen) { stopTracks(got); return; }
  stream = got;
  const video = document.getElementById("cam");
  if (!video) { stopTracks(got); stream = null; return; }
  prepareCamera(video);
  video.srcObject = stream;
  let shown = false;
  const reveal = () => {
    if (gen !== scanGen || shown) return;
    shown = true;
    const lead = screen.querySelector(".lead");
    if (lead) {
      lead.textContent = state.scanFor === "signed"
        ? signedProgress()
        : "Fill the box with the code. Hold steady.";
    }
    const button = document.getElementById("allow-cam");
    if (button) button.hidden = true;
    const bad = screen.querySelector(".bad");
    if (bad && state.scanError === "") bad.textContent = "";
    placeCamera();
    requestAnimationFrame(placeCamera);
  };
  const show = () => {
    if (gen !== scanGen || shown) return;
    let started;
    try { started = video.play(); }
    catch (err) { failCamera(gen, err); return; }
    const done = started && started.then ? started : Promise.resolve();
    done.then(() => {
      if (gen !== scanGen) return;
      reveal();
    }).catch((err) => {
      if (gen !== scanGen || shown || video.readyState < 1) return;
      failCamera(gen, err);
    });
  };
  show();
  video.onloadedmetadata = show;
  setTimeout(() => {
    if (gen !== scanGen || shown) return;
    if (video.readyState >= 2) show();
    else failCamera(gen, { name: "TimeoutError" });
  }, 2000);
  listenForCodes(gen, video);
}

function qrBytes(code) {
  if (!code || !code.chunks) return null;
  const bytes = [];
  for (const chunk of code.chunks) {
    if (chunk.type === "byte" && chunk.bytes) {
      for (const b of chunk.bytes) bytes.push(b);
    }
  }
  return bytes.length ? new Uint8Array(bytes) : null;
}

function readJsQr(video, canvas, ctx) {
  if (typeof jsQR !== "function" || video.readyState < 2) return null;
  const scale = Math.min(1, 640 / (video.videoWidth || 640));
  canvas.width = Math.max(1, Math.floor(video.videoWidth * scale));
  canvas.height = Math.max(1, Math.floor(video.videoHeight * scale));
  ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
  const image = ctx.getImageData(0, 0, canvas.width, canvas.height);
  return jsQR(image.data, image.width, image.height, { inversionAttempts: "attemptBoth" });
}

function listenForCodes(gen, video) {
  const signed = state.scanFor === "signed";
  let detector = null;
  // The signed codes are raw bytes. BarcodeDetector returns text and
  // would change those bytes, so that scan uses jsQR only.
  if (!signed && "BarcodeDetector" in window) {
    try { detector = new BarcodeDetector({ formats: ["qr_code"] }); }
    catch { detector = null; }
  }
  const canvas = document.createElement("canvas");
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  let lastText = "";
  const tick = async () => {
    if (gen !== scanGen) return;
    try {
      let text = "";
      let bytes = null;
      let key = "";
      if (signed) {
        const code = readJsQr(video, canvas, ctx);
        if (code) {
          bytes = qrBytes(code);
          // Two signed pages can both fail as text. Compare the bytes.
          key = bytes ? [...bytes].join(",") : "unreadable";
        }
      } else if (detector) {
        const codes = await detector.detect(video);
        if (codes.length) text = codes[0].rawValue || "";
        key = text;
      } else {
        const code = readJsQr(video, canvas, ctx);
        if (code) text = code.data || "";
        key = text;
      }
      if (!key) lastText = "";
      else if (key !== lastText) {
        lastText = key;
        const err = signed ? await useSigned(bytes) : (
          state.scanFor === "wallet" ? await useWallet(text) : await useDest(text)
        );
        if (state.step !== "scan") return;
        noteScan(err);
      }
    } catch { /* keep looking */ }
    if (gen === scanGen) requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

async function readPhoto(file) {
  if (!file || typeof jsQR !== "function") return;
  const url = URL.createObjectURL(file);
  try {
    const image = new Image();
    image.src = url;
    await image.decode();
    const canvas = document.createElement("canvas");
    const scale = Math.min(1, 1200 / image.width);
    canvas.width = Math.floor(image.width * scale);
    canvas.height = Math.floor(image.height * scale);
    const ctx = canvas.getContext("2d", { willReadFrequently: true });
    ctx.drawImage(image, 0, 0, canvas.width, canvas.height);
    const pixels = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const code = jsQR(pixels.data, pixels.width, pixels.height, { inversionAttempts: "attemptBoth" });
    if (!code) {
      state.scanError = "That photo has no code. Try again, closer.";
      const bad = screen.querySelector(".bad");
      if (bad) bad.textContent = state.scanError;
      return;
    }
    const err = state.scanFor === "signed"
      ? await useSigned(qrBytes(code))
      : (state.scanFor === "wallet" ? await useWallet(code.data) : await useDest(code.data));
    if (state.step !== "scan") return;
    if (err && state.scanFor !== "signed") {
      state.scanError = err;
      render();
      return;
    }
    noteScan(err);
  } finally {
    URL.revokeObjectURL(url);
  }
}

function signedProgress() {
  if (!state.sigCount) return "Scan every code. Start with 00.";
  const have = state.sigParts.map((part) => part.page).sort((a, b) => a - b);
  const missing = [];
  for (let i = 0; i < state.sigCount; i++) {
    if (!have.includes(i)) missing.push(String(i).padStart(2, "0"));
  }
  const got = have.map((n) => String(n).padStart(2, "0")).join(" ");
  if (!missing.length) return "Got " + got + ".";
  return "Got " + got + ". Still need " + missing.join(" ") + ".";
}

function noteScan(err) {
  state.scanError = err || "";
  const bad = screen.querySelector(".bad");
  if (bad) bad.textContent = state.scanError;
  if (state.scanFor === "signed") {
    const lead = screen.querySelector(".lead");
    if (lead) lead.textContent = signedProgress();
  }
}

function clearSigned() {
  state.sigParts = [];
  state.sigCount = 0;
  state.txHex = "";
  state.txHash = "";
  state.sendError = "";
}

function finishPay() {
  sendGen += 1;
  state.page = 0;
  state.amountText = "";
  state.amountFromCode = false;
  state.plan = null;
  state.result = null;
  clearSigned();
  state.step = "home";
  render();
}

function startSignedScan() {
  if (!state.plan || !state.wallet || !state.dest) return;
  state.scanFor = "signed";
  state.scanError = "";
  state.step = "scan";
  cameraAsked = false;
  render();
}

async function useSigned(bytes) {
  if (!state.plan || !state.wallet || !state.dest) return "Go back and check the payment first.";
  if (!bytes) return "That code is not a signed page.";
  let part;
  try { part = parseSigPage(bytes); }
  catch (problem) { return problem.message; }
  if (state.sigCount && part.count !== state.sigCount) return "That code is from a different set.";
  state.sigCount = part.count;
  state.sigParts = state.sigParts.filter((got) => got.page !== part.page);
  state.sigParts.push(part);
  if (state.sigParts.length < state.sigCount) return "";
  try {
    const blob = assembleSigBlob(state.sigParts);
    state.txHex = await buildSignedTx({
      inputs: state.plan.inputs,
      pay: state.plan.pay,
      change: state.plan.change,
      typeByte: state.dest.typeByte,
      hash160: state.dest.hash160,
      walletHash160: state.wallet.hash160,
    }, blob);
  } catch (problem) {
    return problem.message;
  }
  state.step = "send";
  state.sendError = "";
  render();
  broadcast();
  return "";
}

function plainSendError(message) {
  const text = String(message || "");
  if (/missing reference|orphaned|already spent|inputs-missingorspent/i.test(text)) {
    return "The network did not take it. A coin may already be spent.";
  }
  if (/signature|script-verify|mandatory-script/i.test(text)) {
    return "The network did not take it. The signature did not match.";
  }
  if (/fee|dust/i.test(text)) return "The network did not take it. The fee was not enough.";
  if (text && text.length < 140) return text;
  return "The network did not take it.";
}

async function broadcast() {
  const gen = ++sendGen;
  const hex = state.txHex;
  if (!hex) {
    state.sendError = "There is no payment to send.";
    if (state.step === "send") render();
    return;
  }
  try {
    const res = await fetch("https://api.blockcypher.com/v1/doge/main/txs/push", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ tx: hex }),
    });
    const text = await res.text();
    let data = {};
    try { data = JSON.parse(text); }
    catch { data = {}; }
    if (gen !== sendGen || state.step !== "send") return;
    if (!res.ok || data.error) {
      state.sendError = plainSendError(data.error || "The network did not take it.");
      render();
      return;
    }
    state.txHash = String(data.hash || "");
    state.step = "sent";
    render();
  } catch {
    if (gen !== sendGen || state.step !== "send") return;
    state.sendError = "The network did not answer. Check your connection.";
    render();
  }
}

function renderSend() {
  const bits = [
    h("h1", {}, ["Sending"]),
    h("p", { class: "lead" }, [
      state.sendError ? "The payment was not sent." : "The network is taking the payment.",
    ]),
    h("p", { class: "bad" }, [state.sendError]),
  ];
  if (state.sendError) {
    bits.push(h("div", { class: "actions" }, [
      primary("Try again", () => {
        state.sendError = "";
        render();
        broadcast();
      }),
    ]));
  }
  screen.append(...bits);
}

function renderSent() {
  const bits = [
    h("h1", {}, ["Sent"]),
    h("p", { class: "lead" }, ["The payment is on the network."]),
  ];
  if (state.txHash) bits.push(h("p", { class: "addr" }, [spaced(state.txHash)]));
  bits.push(h("div", { class: "actions" }, [
    primary("Done", finishPay),
  ]));
  screen.append(...bits);
}

function startScan(which) {
  rememberPay();
  state.scanFor = which;
  state.scanError = "";
  state.step = "scan";
  cameraAsked = false;
  render();
}

function render() {
  stopCamera();
  parkCamera();
  document.body.classList.toggle("scanning", state.step === "scan");
  document.body.classList.toggle("typing", state.step === "type");
  const step = stepIndex();
  dotsEl.replaceChildren();
  for (let i = 0; i < 4; i++) {
    dotsEl.append(h("span", { class: i === step ? "on" : "" }, ["●"]));
  }
  const showBack = state.step !== "home";
  backBtn.hidden = !showBack;
  backBtn.onclick = () => {
    if (state.step === "scan") {
      if (state.scanFor === "signed") go("type");
      else go(state.scanFor === "dest" ? "pay" : "home");
    } else if (state.step === "typeWallet") go("home");
    else if (state.step === "found") go("home");
    else if (state.step === "pay") go(state.wallet ? "found" : "home");
    else if (state.step === "review") go("pay");
    else if (state.step === "type") go("review");
    else if (state.step === "send") go("type");
    else if (state.step === "sent") finishPay();
  };
  screen.replaceChildren();
  if (state.step === "home") renderHome();
  else if (state.step === "typeWallet") renderTypeWallet();
  else if (state.step === "found") renderFound();
  else if (state.step === "pay") renderPay();
  else if (state.step === "review") renderReview();
  else if (state.step === "type") renderType();
  else if (state.step === "scan") renderScan();
  else if (state.step === "send") renderSend();
  else if (state.step === "sent") renderSent();
}

backBtn.addEventListener("click", () => {});
let wideNow = wide();
window.addEventListener("resize", () => {
  const next = wide();
  if (next !== wideNow) {
    wideNow = next;
    if (state.step === "home") render();
  }
  if (state.step === "scan") placeCamera();
});
if (window.visualViewport) {
  window.visualViewport.addEventListener("resize", () => {
    if (state.step === "scan") placeCamera();
  });
  window.visualViewport.addEventListener("scroll", () => {
    if (state.step === "scan") placeCamera();
  });
}
try {
  render();
} catch (problem) {
  screen.replaceChildren(document.createTextNode(
    problem && problem.message ? problem.message : "This page did not start."
  ));
}
