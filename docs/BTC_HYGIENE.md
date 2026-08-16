# Bitcoin UTXO hygiene rehearsal

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

A local `additiond` can classify operator-supplied Bitcoin address samples
(public on-chain examples or the in-repo fixtures) and emit a signed ADDITION
receipt. This is an **attestation rehearsal**. It does **not** move Bitcoin,
does **not** connect a Bitcoin wallet, and is **not** a BIP-360 consensus
change.

Write RPC stays on `127.0.0.1`. These commands are not on the public-read
allowlist. `hygiene_classify` reads a local fixture file only.

## What the classifier reports

Over `fixtures/btc_hygiene_samples.json` (or another operator-supplied fixture
of the same shape):

* script class: `p2pk`, `p2pkh`, `p2sh`, `p2wpkh`, `p2wsh`, `p2tr`, or `unknown`
* address reuse in the supplied sample set
* pubkey already on-chain (`p2pk` scripts)

## Trusted RPC (loopback)

```bash
printf 'hygiene_classify fixtures/btc_hygiene_samples.json\n' | nc 127.0.0.1 8545
printf 'createwallet hy\n' | nc 127.0.0.1 8545
printf 'hygiene_attest hy 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0\n' | nc 127.0.0.1 8545
# then hygiene_verify <note> — a mutated note is rejected
```

The receipt is an ML-DSA-87 (or the wallet's allowed scheme) attestation
returned as a trailing `note=` field. Label:

`ADDITION-HYGIENE-REHEARSAL|…|moves_bitcoin=0|claim=attestation_not_bip360`

This is not a claim that ADDITION moves Bitcoin. It is not BIP-360. It is not
a live public mainnet and nothing is for sale.

C++ coverage: `test_btc_hygiene`.
