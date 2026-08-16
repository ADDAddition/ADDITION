# Bitcoin UTXO hygiene rehearsal

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

A local `additiond` can classify **operator-supplied** Bitcoin address samples
(public on-chain shapes or the in-repo fixture file) and emit a **signed
ADDITION receipt**. This is an attestation rehearsal.

It does **not** move Bitcoin. It is **not** BIP-360. It is **not** a consensus
change and does **not** claim that ADDITION moves Bitcoin.

Write RPC stays `127.0.0.1`. These commands are trusted/local only. They are
not on the public-read allowlist.

## What the classifier reports

Offline, over `fixtures/btc_hygiene_samples.json` or a path you pass:

* script class: `p2pk`, `p2pkh`, `p2sh`, `p2wpkh`, `p2wsh`, `p2tr`, or `unknown`
* address reuse across the supplied sample set
* pubkey already on-chain (`p2pk` exposes the pubkey in the script)

The fixture file is sample data for rehearsal. It is not a live Bitcoin
connection and does not talk to a wallet.

## Signed receipt

`hygiene_attest` signs a receipt body with the local wallet's ML-DSA-87 (or
another allowed scheme). The receipt is a string, not a ledger `note` field
and not a Bitcoin transaction.

Label:

```text
ADDITION-HYGIENE-REHEARSAL|v1|…|moves_bitcoin=0|claim=attestation_not_bip360
```

`hygiene_verify` checks the body fields and the ML-DSA signature. A mutated
note is rejected (`error: garbage hygiene receipt rejected`). A body that
sets `moves_bitcoin=1` or a BIP-360 claim is rejected.

## Trusted RPC (127.0.0.1:8545)

```bash
printf 'hygiene_classify fixtures/btc_hygiene_samples.json\n' | nc 127.0.0.1 8545
printf 'createwallet alice\n' | nc 127.0.0.1 8545
printf 'hygiene_attest alice 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0\n' | nc 127.0.0.1 8545
# then: hygiene_verify <note>
```

C++ coverage: `test_btc_hygiene`.

This is a research prototype / local node path. Not a live public mainnet.
Nothing for sale.
