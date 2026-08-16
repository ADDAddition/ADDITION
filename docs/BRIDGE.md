# ADDITION bridge (not built)

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

Status: **specification only**. This file is an in-repo note. It is **not** a
live cross-chain product. It does **not** move Bitcoin, Ethereum, or Solana.
Nothing here is for sale. The public site and public RPC stay testnet.

`moves_bitcoin=0` `moves_eth=0` `moves_sol=0`

Write RPC stays `127.0.0.1`. Never open public `8545`. No site flip. No public
`/bridge` page. No BTC/ETH/SOL → ADD widget.

## What exists today (not a bridge)

These paths are already in the tree. None of them is a live ADDITION bridge.

* In-process `bridge_register` / `bridge_lock` / `bridge_mint` / `bridge_burn` /
  `bridge_release` / `bridge_balance` (`BridgeEngine`). They increment local
  counters and wrapped balances in `bridge.dat`. A string label such as `btc`
  is not observation of Bitcoin. `lock` here does not lock coins on another
  chain. `mint` here does not mint ADD from a foreign deposit.
* Local EVM bootstrap `web/evm/evm_rpc_bridge.py` on `127.0.0.1:9545`.
  `eth_sendRawTransaction` is disabled. Not Ethereum custody.
* Bitcoin UTXO hygiene (`hygiene_classify` / `hygiene_attest` /
  `hygiene_verify`). Signed ADDITION receipt labeled
  `ADDITION-HYGIENE-REHEARSAL` with `moves_bitcoin=0`. Does not move Bitcoin.
  See [BTC_HYGIENE.md](BTC_HYGIENE.md).
* Localhost AMM (`swap_pool_create` / `swap_exact_in` / `swap_tvl`) on
  `127.0.0.1` write RPC only. Same-chain pool math. Not a bridge.

Public-read RPC refuses every `bridge_*` write (`error: command disabled on
public RPC`). `bridge_lock` / `bridge_mint` / `bridge_burn` / `bridge_release`
are not on the public-read allowlist.

## What a real ADDITION bridge would need

A real bridge is ADDITION-native. It is not a rename of the in-process map.
It does not import another project's bridge, messenger, or AMM.

Minimum pieces before any `moves_*=1` claim:

1. **Foreign-chain observation.** A watcher must see a confirmed lock or burn
   on the source chain using that chain's own confirmation rules. Registering
   the string `btc`, `eth`, or `sol` is not observation.
2. **Source-chain custody the node can check.** Bitcoin: a script / multisig
   the node can verify from Bitcoin headers or a Bitcoin node. Ethereum: a
   contract whose logs the node can verify from Ethereum headers or an
   Ethereum node. Solana: a program whose accounts the node can verify from
   Solana. A local `locked_pool` integer is not custody.
3. **ADDITION mint policy.** Credit only after the observation is verified.
   Replay protection must bind the foreign txid / slot / signature, not a
   local hash of `chain|user|amount`.
4. **Burn-and-release.** Burn ADDITION-side inventory, then prove the release
   on the source chain before that asset can be spent there.
5. **Reorg and key-loss handling.** If the source chain reorgs below the
   required confirmations, ADDITION must not keep the credit. If an operator
   key is lost, mint must stop. Incomplete confirmations must not credit.
6. **Write surface.** Any future lock/mint write stays on `127.0.0.1` until
   the observation path exists. Public-read RPC refuses those writes. No
   public `/bridge` page.

`moves_bitcoin=1` / `moves_eth=1` / `moves_sol=1` are allowed only after the
matching observation + custody + mint path is proven against that chain.
Until then those flags stay `0`.

This spec does not publish a transactions-per-second figure. It does not
add a zero-knowledge proof of a foreign lock. Those are not substitutes
for observation and custody.

## What is not built

* No Bitcoin lock script watched by ADDITION
* No Ethereum lock contract watched by ADDITION
* No Solana program watched by ADDITION
* No mint of ADD from a foreign-chain deposit
* No release of BTC, ETH, or SOL from an ADDITION burn
* No public `/bridge` or `/swap` product page
* No BTC/ETH/SOL → ADD widget
* No token sale
* No seed rebuild
* `ADDITION_MAINNET_V1` difficulty is unchanged (`0x000000FFFFFFFFFF`)

## First implementation slice (later; not in this tree yet)

A later slice may add a **local rehearsal** only. If that code is added, it
must satisfy all of:

* Write RPC on `127.0.0.1` only
* Command name and replies labeled `rehearsal`
* Reply includes `moves_bitcoin=0 moves_eth=0 moves_sol=0`
* Public-read RPC refuses it (`error: command disabled on public RPC`)
* It must not implement lock/mint that can be read as moving Bitcoin,
  Ethereum, or Solana
* It must not remake #12, #33, #38, or #39
* It must not loosen `ADDITION_MAINNET_V1` difficulty
* It must not rebuild the seed

This tree ships the spec only. A rehearsal command is omitted because a new
lock/mint RPC that names Bitcoin, Ethereum, or Solana would be read as a
live product.

## Do not import

Read other public docs only to name the problem. Do not copy bridge,
messaging, or AMM code from Wormhole, LayerZero, Sushi, Uniswap, or any
other chain.

## Hard no

* No public `/bridge` or `/swap` page
* No BTC/ETH/SOL → ADD widget or claim
* No seed rebuild
* Do not remake #12 #33 #38 #39
* Do not loosen `ADDITION_MAINNET_V1` difficulty
* Never bind write RPC on a public address
* No site flip
* No token sale

This is a research prototype / local-node note. Not a live public mainnet.
Not a live cross-chain product.
