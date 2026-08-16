# What a local ADDITION node proved (research testnet)

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

This page lists **only** what a running local `additiond` and the in-repo tests
are meant to demonstrate. It is not a market, not a mainnet, and not a
compatibility claim.

Write RPC stays on `127.0.0.1:8545`. Do not bind it to `0.0.0.0`.
Public read RPC (opt-in) is allowlisted on port `38545`.

`max_supply=50000000` is a kernel design parameter, not a circulating supply
or a sale.

## Build

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`--regtest` keeps header PoW trivial so laptop tests finish. Shared-testnet
(`--network testnet`) uses a min-diff floor so a laptop cannot emit ~36 ms
public blocks. That floor is **not** economic security.

## Slice 1 — hash-committed addresses + ML-DSA context + crypto_selftest

**Proved when the local node answers:**

* `createwallet` returns a 128-hex address (`address_chars=128`) and
  `pub_bytes=2592` for ML-DSA-87. The address is
  `SHA3-512(scheme_id || 0x00 || pubkey_bytes)`, not the raw key.
* A PQ `wallet_send` mines; `getblock` / `tx_signers=` show the hash-address,
  not the 2592-byte key.
* `crypto_selftest` (public-read allowlist) prints values this process just
  computed: `scheme`, `pk_bytes`, `sig_bytes`, `sha3_512`, `pq_mode=strict`,
  `liboqs`, `openssl`, `sign_verify=ok`, `empty_ctx_rejected=1`,
  `allowed_sig_algs`. It does not print theoretical TPS.
* Consensus signatures use hedged `OQS_SIG_sign_with_ctx_str` with a non-empty
  FIPS 204 context `ADDITION|<mode>|<chain-id>|<genesis-hash>` (≤255 bytes).
  The same message with an empty or wrong context fails verify.

```bash
./build/additiond --regtest --data-dir /tmp/addition-s1 --local-rpc-port 18545
printf 'crypto_selftest\n' | nc 127.0.0.1 18545
printf 'createwallet alice\n' | nc 127.0.0.1 18545
printf 'getinfo\n' | nc 127.0.0.1 18545
```

C++ coverage: `test_hash_address` (wrong-ctx reject + hash-address bind +
garbage pubkey spend reject).

## Slice 2 — Bitcoin UTXO hygiene report + signed ADDITION receipt

Offline classifier over **in-repo fixtures** (script type, address reuse,
pubkey-already-on-chain). It does **not** connect a wallet and does **not**
move Bitcoin. It is a rehearsal / attestation. **Not BIP-360.**

```bash
# classify the checked-in samples (no network)
printf 'hygiene_classify fixtures/btc_hygiene_samples.json\n' | nc 127.0.0.1 18545

# after createwallet + a mined coinbase to that wallet:
printf 'hygiene_attest alice 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0\n' | nc 127.0.0.1 18545
# then hygiene_verify <note> — a mutated note is rejected
```

The receipt is a normal ADDITION transaction `note=` plus an ML-DSA-87
attestation. `tx_status` / `getblock` can show that note. Label:
`ADDITION-HYGIENE-REHEARSAL`, `moves_bitcoin=0`,
`claim=attestation_not_bip360`.

C++ coverage: `test_btc_hygiene`.

## Slice 3 — two-node sync + confirmations + min-diff

```bash
# two processes, write RPC on loopback only
./build/additiond --regtest --data-dir /tmp/a --local-rpc-port 18545 --p2p-port 29545
ADDITION_ENABLE_P2P_RPC=1 ./build/additiond --regtest --data-dir /tmp/b \
  --local-rpc-port 18546 --p2p-port 29546 --bootstrap 127.0.0.1:29545
printf 'addpeer 127.0.0.1:29545\n' | nc 127.0.0.1 18546
printf 'mine miner-a\n' | nc 127.0.0.1 18545
printf 'sync\n' | nc 127.0.0.1 18546
printf 'getblockhash 1\n' | nc 127.0.0.1 18545
printf 'getblockhash 1\n' | nc 127.0.0.1 18546
```

`getinfo` exposes `confirmations_policy` and `economic_security=none`.
`wallet_send` waits until that many local confirmations (still not economic
security). Shared-testnet rejects a toy-diff header
(`difficulty below min-diff floor`).

Automated: `test_confirmations`, `test_two_node_sync`.

## Slice 4 — opt-in SLH-DSA vault

Default remains ML-DSA-87 / `pq_mode=strict`. Opt-in address type
`slh-dsa-shake-256s` (FIPS 205 parameter set) via liboqs
`SPHINCS+-SHAKE-256s-simple` when that algorithm is present.

```bash
printf 'createwallet vault slh-dsa-shake-256s\n' | nc 127.0.0.1 18545
printf 'getinfo\n' | nc 127.0.0.1 18545   # allowed_sig_algs=
printf 'crypto_selftest\n' | nc 127.0.0.1 18545
```

`scheme_id` is inside the address hash so both types can coexist. Unknown
schemes are rejected in strict mode. This does **not** make the chain
hash-based. Falcon / FN-DSA are not added. No FIPS 140-3 claim.

If this liboqs build lacks SPHINCS+-SHAKE-256s-simple, **or** that algorithm
cannot `OQS_SIG_sign_with_ctx_str` (liboqs 0.12.0 SPHINCS+ returns failure
for a non-empty context), keygen/spend fail closed. `test_slh_dsa` records
that instead of signing without context or faking a verify. The `scheme_id`
address hook still distinguishes the two types.

## Not claimed

* Mainnet, token or node sale, WalletConnect, gcli.ai, Addison Electronics
* Invented TPS, fees, peer counts, hashrate, or public economic security
* Groth16, confidential balances, Alpenglow, Falcon-as-FIPS, Uniswap, AI-PoUW
* BIP-360 compatibility, moving Bitcoin, or a Bitcoin bridge
* That shared-testnet min-diff is miner-market security
* That ADDITION is a hash-based signature chain, or FIPS 140-3 validated
