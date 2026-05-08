"""
Free, no-API-key Dogecoin UTXO fetching + broadcast.

We try multiple providers in order. Each returns a normalized list of UTXOs
with the fields we need to build a transaction:

    [
      {
        "txid": str,            # hex, big-endian display order
        "vout": int,
        "value": int,           # koinu
        "script_pubkey": bytes, # the prevout's scriptPubKey
        "confirmations": int,
        "height": int | None,
      },
      ...
    ]

Providers (no API key required):
  - BlockCypher       https://api.blockcypher.com/v1/doge/main
  - Chainz CryptoID   https://chainz.cryptoid.info/doge/api.dws
  - Tatum (community)? — skipped, requires key

Broadcast endpoints supported (tried in order):
  - SoChain      https://chain.so/api/v2/send_tx/DOGE
  - Blockchair   https://api.blockchair.com/dogecoin/push/transaction
  - BlockCypher  /v1/doge/main/txs/push
  - Chainz CryptoID  ?q=pushtx
"""

import logging
from typing import List, Optional

import requests

from dogecoin import (
    DecodedAddress,
    decode_address,
    script_pubkey_p2pkh,
    script_pubkey_p2sh,
)

log = logging.getLogger("utxo_api")

DEFAULT_TIMEOUT = 15
USER_AGENT = "DogeGB-Companion/1.0"


class UtxoFetchError(Exception):
    pass


class BroadcastError(Exception):
    pass


# --- Helpers ---------------------------------------------------------------

def _hexstr_to_bytes(s: str) -> bytes:
    return bytes.fromhex(s)


def _expected_script_for(decoded: DecodedAddress) -> bytes:
    if decoded.address_type == "p2pkh":
        return script_pubkey_p2pkh(decoded.hash160)
    return script_pubkey_p2sh(decoded.hash160)


# --- Provider: BlockCypher -------------------------------------------------

def fetch_utxos_blockcypher(address: str) -> List[dict]:
    """
    BlockCypher returns up to 200 txrefs by default. We pull with includeScript
    so we don't have to look up each prev tx separately.
    """
    decoded = decode_address(address)
    url = (
        f"https://api.blockcypher.com/v1/doge/main/addrs/{address}"
        f"?unspentOnly=true&includeScript=true&limit=200"
    )
    log.info("blockcypher: GET %s", url)
    r = requests.get(
        url, headers={"User-Agent": USER_AGENT}, timeout=DEFAULT_TIMEOUT
    )
    if r.status_code == 429:
        raise UtxoFetchError("BlockCypher rate-limited (HTTP 429)")
    if r.status_code != 200:
        raise UtxoFetchError(f"BlockCypher HTTP {r.status_code}: {r.text[:200]}")
    data = r.json()

    utxos = []
    for ref in data.get("txrefs", []):
        if ref.get("spent"):
            continue
        if ref.get("tx_input_n", -1) != -1:
            # tx_input_n == -1 means this is an OUTPUT ref, which is what we want
            continue
        spk_hex = ref.get("script") or _expected_script_for(decoded).hex()
        utxos.append(
            {
                "txid": ref["tx_hash"],
                "vout": ref["tx_output_n"],
                "value": int(ref["value"]),
                "script_pubkey": _hexstr_to_bytes(spk_hex),
                "confirmations": int(ref.get("confirmations", 0)),
                "height": ref.get("block_height"),
            }
        )
    # Also unconfirmed_txrefs if present
    for ref in data.get("unconfirmed_txrefs", []):
        if ref.get("spent"):
            continue
        if ref.get("tx_input_n", -1) != -1:
            continue
        spk_hex = ref.get("script") or _expected_script_for(decoded).hex()
        utxos.append(
            {
                "txid": ref["tx_hash"],
                "vout": ref["tx_output_n"],
                "value": int(ref["value"]),
                "script_pubkey": _hexstr_to_bytes(spk_hex),
                "confirmations": 0,
                "height": None,
            }
        )
    return utxos


# --- Provider: chainz.cryptoid.info ---------------------------------------

def fetch_utxos_chainz(address: str) -> List[dict]:
    """
    chainz.cryptoid.info offers a free, keyless `unspent` endpoint. The script
    field isn't always returned, so we synthesize the scriptPubKey from the
    address (which is fine — we know it must equal that script since these
    UTXOs are addressed to it).
    """
    decoded = decode_address(address)
    url = (
        f"https://chainz.cryptoid.info/doge/api.dws"
        f"?q=unspent&active={address}"
    )
    log.info("chainz: GET %s", url)
    r = requests.get(
        url, headers={"User-Agent": USER_AGENT}, timeout=DEFAULT_TIMEOUT
    )
    if r.status_code != 200:
        raise UtxoFetchError(f"chainz HTTP {r.status_code}: {r.text[:200]}")
    try:
        data = r.json()
    except ValueError:
        raise UtxoFetchError(f"chainz returned non-JSON: {r.text[:200]}")

    if isinstance(data, dict) and "error" in data:
        raise UtxoFetchError(f"chainz error: {data['error']}")

    spk = _expected_script_for(decoded)
    raw_list = data.get("unspent_outputs") if isinstance(data, dict) else None
    if raw_list is None:
        # chainz returns the list directly in some variants
        raw_list = data if isinstance(data, list) else []

    utxos = []
    for u in raw_list:
        # Normalize field names across variants
        txid = u.get("tx_hash") or u.get("txid") or u.get("tx_hash_big_endian")
        vout = u.get("tx_output_n", u.get("vout", u.get("n")))
        value = u.get("value")
        confirmations = u.get("confirmations", 0)
        if value is None or txid is None or vout is None:
            log.warning("skipping malformed utxo: %s", u)
            continue
        utxos.append(
            {
                "txid": str(txid),
                "vout": int(vout),
                "value": int(value),
                "script_pubkey": spk,
                "confirmations": int(confirmations),
                "height": None,
            }
        )
    return utxos


# --- Public API ------------------------------------------------------------

UTXO_PROVIDERS = [
    ("blockcypher", fetch_utxos_blockcypher),
    ("chainz", fetch_utxos_chainz),
]


def fetch_utxos(address: str) -> List[dict]:
    """
    Tries each provider in order. Returns the first successful result.
    Raises UtxoFetchError if all providers fail.
    """
    decode_address(address)  # validate first
    last_err: Optional[Exception] = None
    for name, fn in UTXO_PROVIDERS:
        try:
            log.info("trying %s for UTXOs", name)
            utxos = fn(address)
            log.info("%s returned %d UTXOs", name, len(utxos))
            return utxos
        except Exception as e:
            log.warning("provider %s failed: %s", name, e)
            last_err = e
            continue
    raise UtxoFetchError(f"All UTXO providers failed; last error: {last_err}")


# --- Broadcast -------------------------------------------------------------

def broadcast_blockcypher(tx_hex: str) -> str:
    url = "https://api.blockcypher.com/v1/doge/main/txs/push"
    log.info("blockcypher: POST %s", url)
    r = requests.post(
        url,
        json={"tx": tx_hex},
        headers={"User-Agent": USER_AGENT},
        timeout=DEFAULT_TIMEOUT,
    )
    if r.status_code not in (200, 201):
        raise BroadcastError(f"BlockCypher push failed HTTP {r.status_code}: {r.text[:300]}")
    data = r.json()
    txid = data.get("tx", {}).get("hash") or data.get("hash")
    if not txid:
        raise BroadcastError(f"BlockCypher response missing txid: {data}")
    return txid


def broadcast_chainz(tx_hex: str) -> str:
    url = f"https://chainz.cryptoid.info/doge/api.dws?q=pushtx&active={tx_hex}"
    log.info("chainz: GET %s (truncated)", url[:80])
    r = requests.get(
        url, headers={"User-Agent": USER_AGENT}, timeout=DEFAULT_TIMEOUT
    )
    if r.status_code != 200:
        raise BroadcastError(f"chainz push HTTP {r.status_code}: {r.text[:300]}")
    text = r.text.strip().strip('"')
    if len(text) == 64 and all(c in "0123456789abcdefABCDEF" for c in text):
        return text
    raise BroadcastError(f"chainz unexpected response: {r.text[:300]}")


def broadcast_sochain(tx_hex: str) -> str:
    url = "https://chain.so/api/v2/send_tx/DOGE"
    log.info("sochain: POST %s", url)
    r = requests.post(
        url,
        json={"tx_hex": tx_hex},
        headers={"User-Agent": USER_AGENT},
        timeout=DEFAULT_TIMEOUT,
    )
    if r.status_code != 200:
        raise BroadcastError(f"sochain push HTTP {r.status_code}: {r.text[:300]}")
    data = r.json()
    if data.get("status") != "success":
        raise BroadcastError(f"sochain error: {data}")
    txid = data.get("data", {}).get("txid")
    if not txid:
        raise BroadcastError(f"sochain response missing txid: {data}")
    return txid


def broadcast_blockchair(tx_hex: str) -> str:
    url = "https://api.blockchair.com/dogecoin/push/transaction"
    log.info("blockchair: POST %s", url)
    r = requests.post(
        url,
        data={"data": tx_hex},
        headers={"User-Agent": USER_AGENT},
        timeout=DEFAULT_TIMEOUT,
    )
    if r.status_code != 200:
        raise BroadcastError(f"blockchair push HTTP {r.status_code}: {r.text[:300]}")
    data = r.json()
    txid = data.get("data", {}).get("transaction_hash")
    if not txid:
        raise BroadcastError(f"blockchair response missing txid: {data}")
    return txid


BROADCASTERS = [
    ("sochain", broadcast_sochain),
    ("blockchair", broadcast_blockchair),
    ("blockcypher", broadcast_blockcypher),
    ("chainz", broadcast_chainz),
]


def broadcast_tx(tx_hex: str) -> str:
    """
    Broadcasts the signed tx. Tries providers in order; returns txid on success.
    """
    last_err: Optional[Exception] = None
    for name, fn in BROADCASTERS:
        try:
            log.info("broadcasting via %s", name)
            return fn(tx_hex)
        except Exception as e:
            log.warning("broadcaster %s failed: %s", name, e)
            last_err = e
    raise BroadcastError(f"All broadcasters failed; last error: {last_err}")
