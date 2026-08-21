# ADDITION_FINAL - Final Command Surface

## RPC Endpoints
- Local-only trusted RPC: `127.0.0.1:8545` (all commands; optional `ADDITION_RPC_TOKEN`)
- Public read RPC (opt-in): `0.0.0.0:38545` — allowlist only, no auth token
- LAN RPC: `0.0.0.0:18545` (off unless `ADDITION_ENABLE_LAN_RPC=1` + `ADDITION_LAN_RPC_TOKEN`)
- P2P transport RPC: `0.0.0.0:28545` (off unless `ADDITION_ENABLE_P2P_RPC=1`)

Each TCP request is one command line and returns one response line.

### Public read RPC
Enable with `--public-rpc` or `ADDITION_ENABLE_PUBLIC_RPC=1`:

```bash
./build/additiond --network testnet --public-rpc
# or
ADDITION_ENABLE_PUBLIC_RPC=1 ./build/additiond --network testnet
```

Allowlist (everything else returns `error: command disabled on public RPC`):
- `getinfo`
- `monetary_info`
- `crypto_selftest`
- `tx_status <tx_hash>`
- `peers`
- `getblock <height_or_hash>`
- `getblockhash <height>`
- `getblockraw <height>`

Not on the public port: `mine`, `sendtx*`, `createwallet`, `wallet_*`, identity rotation, admin, contract/token writes.

TCP and a tiny HTTP adapter share the same port:

```bash
curl 'https://rpc.additionblockchain.com/rpc?cmd=getinfo'
curl 'http://34.27.30.115/rpc?cmd=getinfo'
curl 'http://34.27.30.115:38545/rpc?cmd=getinfo'
curl 'http://34.27.30.115:38545/jsonrpc?method=getinfo'
```

Those curls succeed only when the operator seed answers. If they timeout, run a local `--public-rpc` node or `scripts/start_two_node_testnet.sh`. Do not treat a timeout as a secret second seed.

Path is `/rpc?cmd=getinfo` or `/jsonrpc?method=getinfo`, not `/getinfo`. `:80` works when `38545` is filtered.

Public-read JSON API (same allowlist, no writes). Not Ethereum JSON-RPC.

```bash
GET  http://HOST:38545/jsonrpc?method=getinfo
GET  http://HOST:38545/jsonrpc?method=getblock&params=0
GET  http://HOST:38545/jsonrpc?method=getblockraw&params=0
POST http://HOST:38545/jsonrpc
{"jsonrpc":"2.0","id":1,"method":"monetary_info","params":[]}
```

Join the operator testnet: `--bootstrap 34.27.30.115:28545`. `sync` uses HTTP `:80` then `:38545` (`getblockraw`). HTTP `:80` is the reliable path; public TCP 28545 can timeout or be filtered. Seed operators set `ADDITION_ADVERTISED_P2P=34.27.30.115:28545` so public `getinfo` / `peers` do not list `self`. Write RPC stays `127.0.0.1`.

Override bind/port with `--public-rpc-bind`, `--public-rpc-port`, `ADDITION_PUBLIC_RPC_BIND`, or `ADDITION_PUBLIC_RPC_PORT`. HTTP replies send `Access-Control-Allow-Origin: *` (read-only allowlist, no cookies), `Access-Control-Allow-Methods: GET, OPTIONS`, `OPTIONS` 204, and `Cache-Control: no-store`. `curl /rpc?cmd=getinfo` is unchanged.

Two-node local testnet (write RPC stays loopback):

```bash
./scripts/start_two_node_testnet.sh
```

Node A: `--public-rpc` on `38545`, P2P `28545`, write `127.0.0.1:8545`.
Node B: `--data-dir` second tree, write `8546`, P2P `28546`, `--bootstrap 127.0.0.1:28545`.
Operator public P2P (IPv4 only): `--bootstrap 34.27.30.115:28545`. Write RPC stays loopback.
See [TWO_NODE_TESTNET.md](TWO_NODE_TESTNET.md).

Website `PUBLIC_RPC_HTTP` stays empty in `web/public/wrangler.toml` so a down node shows `RPC offline`. Set it only to a real public-rpc HTTP URL you operate. Do not commit trycloudflare URLs.

## Core chain
- `getinfo` — trusted extra fields include `require_privacy_pool` and `privacy_master_key=set|missing`
- `fee_info` — `base_min_fee`, `recommended_min_fee`, `ai_fee_floor` (LAN-allowed read)
- `monetary_info`
- `crypto_selftest`
- `createwallet [name] [scheme]` — default ML-DSA-87; optional `slh-dsa-shake-256s` only if this liboqs can `OQS_SIG_sign_with_ctx_str` with a non-empty context (otherwise rejected in strict mode). Unknown schemes rejected. Writes `data/wallets/<name>.wal` (0600); returns address/pub/name/path; `priv_printed=0`
- `wallet_list`
- `wallet_info <name>`
- `wallet_balance <name>`
- `wallet_send <name> <to_addr> <amount> [fee]` — signs from the local file; no privkey on the wire
- `wallet_sign <name> <message_hex_utf8>` — same as `sign_message` without sending the key
- `hygiene_classify [path]` — offline Bitcoin script hygiene over operator samples or `fixtures/btc_hygiene_samples.json`. Trusted RPC only. Does not move Bitcoin. Not BIP-360.
- `hygiene_attest <wallet> <btc_addr> <height> <class> [reuse] [pubkey_on_chain]` — signed ADDITION receipt (`ADDITION-HYGIENE-REHEARSAL`, `moves_bitcoin=0`, `claim=attestation_not_bip360`). Attestation rehearsal, not a consensus change.
- `hygiene_verify <receipt_note>` — verify the signed receipt; a mutated note is rejected
- `getbalance <address>`
- `getbalance_instant <address>`
- `tx_build <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce>`
- `sendtx_signed <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>`
- `sendtx_signed_hash <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>`
- `sendtx <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce>` (legacy, disabled unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`)
- `sendtx_hash <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce>` (legacy, disabled unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`)
- `tx_status <tx_hash>`
- `getblock <height_or_hash>`
- `getblockhash <height>`
- `getblockraw <height>`
- `mine` — trusted RPC / stdin only. Testnet: SHA3-512 header PoW, 30s deadline (`mine_deadline_sec=30`). Mainnet *profile*: multi-thread `memory_hard` at `0x000000FFFFFFFFFF`, no 30s deadline (`mine_deadline_sec=0`; runs until a block is found). Not a live public network. Optional in-process auto-mine (`--auto-mine`, `ADDITION_AUTO_MINE=1`) is off by default, testnet only, and is not a public RPC command. `getinfo` reports `pow_algorithm` and `auto_mine=off|on`.

## P2P + Consensus
- `addpeer <ip:port>`
- `delpeer <ip:port>`
- `peers`
- `vote <peer> <height> <block_hash>`
- `quorum <height> <block_hash>`
- `peer_inbound <peer> <payload>`
- `gossip_flush`
- `sync` — requires at least one peer; returns `error: no peer` if the set is empty. Success is `ok:height=<n>` only after a handshake or public-rpc ingest with a listed peer.
- `protocol_status` — local measured mine/verify fields plus `privacy_claim=opening_not_zk`. `research_goal_tps=100000` is labeled `research_goal_is_not_a_measurement=true`.
- `benchmark_objective <blocks> <verify_samples>` — parallel ML-DSA-87 verify plus empty-header mines. Does not inject mempool junk. A later `mine` must still succeed.
- `node_pubkey`
- `identity_rotate_propose <new_pubkey_hex> <new_privkey_hex> <grace_seconds>`
- `identity_rotate_vote <peer_id>`
- `identity_rotate_vote_broadcast`
- `identity_rotate_commit`
- `identity_rotate_status`

### P2P transport payload protocol
- TX payload binary codec now uses version marker `TXB2` with checksum trailer.
- Block payload binary codec now uses version marker `BLB2` with checksum trailer.
- Decoder remains backward-compatible with `TXB1` / `BLB1` payloads for rolling upgrades.
- Strict handshake required before peer message processing:
	- request: `HELLO|2|<network_id>|<unix_ts>|<nonce>|<peer_pubkey>|<peer_signature>`
	- response: `HELLO_ACK|2|<network_id>|<unix_ts>|<echo_nonce>|<responder_pubkey>|<responder_signature>`
	- testnet `network_id` is `ADDITION_TESTNET_V1` (default). There is no live public mainnet.
	- inbound timestamp skew window: ±90s
	- nonce replay is rejected per peer (rolling anti-replay set)
	- signatures are validated with PQ verification (`ml-dsa-87`) over the signed handshake body
	- mismatched protocol or network id is rejected and peer score is penalized.

	### Controlled node identity rotation
	- Rotation is staged (not immediate): `identity_rotate_propose ...`
	- Activation only after:
		- grace period elapsed
		- quorum reached (`2/3 + 1` over `peers + self`)
	- Votes are registered through `identity_rotate_vote <peer_id>`.
	- Signed network vote broadcast available via `identity_rotate_vote_broadcast`.
	- Final switch by `identity_rotate_commit`.
	- Current state visible with `identity_rotate_status`.

	### Rotation gossip messages
	- `IDROTATE|<rotation_id>|<old_pubkey>|<new_pubkey>|<effective_after>|<proof_sig>`
	- `IDVOTE|<rotation_id>|<voter_id>|<voter_pubkey>|<vote_sig>`

	Both messages are signature-verified before being accepted.
	Rotation messages are auto-relayed to connected peers after handshake, with deduplication to limit relay loops.

	### P2P inbound rate limits
	- Generic messages: max `120` per peer per `10s` sliding window.
	- Expensive messages (`REQBLK`, `REQINV`, `BLKDATA`, `IDROTATE`, `IDVOTE`): max `24` per peer per `10s` sliding window.
	- Exceeding limits triggers peer penalty through existing scoring/ban path.

	### Transport hardening
	- Max request/response line size enforced: `32768` bytes.
	- Socket send/receive timeouts enforced on client and server sockets: `4000ms`.
	- Oversized requests are rejected with transport error and do not reach command handlers.

	### Parser field bounds (security)
	- `peer_id` max length: `128`
	- `nonce` max length: `128`
	- `rotation_id` max length: `128`
	- `pubkey(hex)` max length: `12000`
	- `signature(hex)` max length: `40000`

	Inbound `HELLO/HELLO_ACK`, `IDROTATE`, and `IDVOTE` exceeding these bounds are rejected and penalized.

	### PQ key/signature validation hardening
	- Strict hex validation before decode: charset, even length, max size.
	- Private key hex length must exactly match ML-DSA-87 secret key size.
	- Public key hex length must exactly match ML-DSA-87 public key size.
	- Signature hex must be non-empty and within ML-DSA-87 max signature size.
	- Any mismatch fails signing/verification immediately.

## Privacy pool
- `privacy_note_prepare <amount>` — returns `trapdoor`, `commitment`, `nullifier` for the SHA3-512 opening relation
- `privacy_mint_open <owner> <amount> <commitment_hex> <nullifier_hex> <trapdoor_hex>`
- `privacy_spend_open <owner> <note_id> <recipient> <amount> <trapdoor_hex>`
- `privacy_status` — reports `opening_verifier=sha3_opening` and `claim=opening_not_zk`
- `privacy_native_verifier <pq_mldsa87>`
- `privacy_mint_zk <owner> <amount> <commitment_hex> <nullifier_hex> <proof_hex> <vk_hex>`
- `privacy_spend_zk <owner> <note_id> <recipient> <amount> <nullifier_hex> <proof_hex> <vk_hex>`

Verifier notes:
- The real proving path in this tree is **SHA3-512 commitment + nullifier opening**. The verifier recomputes `SHA3-512("cm|v1|"+amount+"|"+trapdoor)` and `SHA3-512("nf|v1|"+commitment+"|"+trapdoor)`. A spent commitment cannot be reminted. A garbage trapdoor is rejected. The node sees the opening. This is **not** zero-knowledge, not Groth16, not Bulletproofs, not ZK-Shield.
- `privacy_mint_zk` / `privacy_spend_zk` still verify an ML-DSA-87 signature of `mint|...` / `spend|...`. That is a signature wrap, not a circuit. Keep those commands only for compatibility.
- Note storage hardening: `owner` and `amount` are not persisted in plaintext (`ADDITION_PRIVACY_MASTER_KEY`, minimum 32 chars).
- `owner_tag` derivation is keyed with `ADDITION_PRIVACY_MASTER_KEY`.
- Write privacy commands stay off the public RPC allowlist.

## Staking
- `stake <address> <amount>`
- `unstake <address> <amount>`
- `staked <address>`
- `stake_reward <amount>`
- `stake_claim <address>`

## Smart-contract runtime
- `contract_deploy <owner> <code>`
- `contract_call <id> <set|add|get|token_balance|swap_quote|zk_mint|zk_spend|zk_privacy_status> <key> <value>`

### Leftover `contract_call` aliases (ML-DSA wrap, not a ZK circuit)
- `zk_mint`
	- key format: `<OWNER>:<COMMITMENT_HEX>:<NULLIFIER_HEX>:<PROOF_HEX>:<VK_HEX>`
	- value: amount to mint (`> 0`)
	- return: created `note_id` after an ML-DSA-87 wrap check
- `zk_spend`
	- key format: `<OWNER>:<NOTE_ID>:<RECIPIENT>:<NULLIFIER_HEX>:<PROOF_HEX>:<VK_HEX>`
	- value: amount to send (`> 0`)
	- return: recipient `note_id`
- `zk_privacy_status`
	- key format: any non-empty token (for example `status`)
	- value: ignored (set `0`)
	- return: `privacy_mode=sha3_opening`, `privacy_ok=true`, `privacy_verifier=sha3_opening`, `claim=opening_not_zk`, notes and nullifier stats

Notes:
- The real path is SHA3-512 opening (`privacy_mint_open` / `privacy_spend_open`).
- `zk_mint` and `zk_spend` require valid proof/vk hex and in-process ML-DSA-87 verification. They are not Groth16 / Bulletproofs / SNARK.

## Token & NFT runtime
- `token_create <symbol> <owner> <max_supply> <initial_mint>`
- `token_mint <symbol> <caller> <to> <amount>`
- `token_transfer <symbol> <from> <to> <amount>` — unsigned local research (opaque names)
- `token_sign_payload <symbol> <from> <to> <amount>` — canonical `token_transfer|…` string
- `token_transfer_signed <symbol> <from> <to> <amount> <pubkey_hex> <sig_hex>` — ML-DSA-87; `from` must be the hash-committed address of `pubkey`
- `token_transfer_wallet <wallet> <symbol> <to> <amount>` — signs from the local `.wal`; `from` is the wallet address
- `token_burn <symbol> <from> <amount>`
- `token_balance <symbol> <owner>`
- `token_info <symbol>`
- `nft_mint <collection> <token_id> <owner> <metadata>` — metadata may be a URL or hash
- `nft_transfer <collection> <token_id> <from> <to>`
- `nft_owner <collection> <token_id>`
- `nft_info <collection> <token_id>` — `owner=` plus stored `metadata=`

## Swap (in-process pool math)
- `swap_pool_create <token_a> <token_b> <fee_bps>`
- `add_liquidity` / `swap_add_liquidity <token_a> <token_b> <provider> <amount_a> <amount_b>`
- `swap_quote <token_in> <token_out> <amount_in>`
- `swap_exact_in <token_in> <token_out> <trader> <amount_in> <min_out>`
- `swap_exact_in_wallet <wallet> <token_in> <token_out> <amount_in> <min_out>` — signs from the local `.wal`; trader is the wallet address
- `swap_best_route_exact_in_signed` — existing PQ-signed multi-hop path
- `swap_pool_info <token_a> <token_b>`
- `swap_tvl` — sum of live pool reserves from `swap_pool_info`. `0` if no pools. Not a made-up TVL.

## Bridge runtime
- `bridge_register <chain>`
- `bridge_lock <chain> <user> <amount>`
- `bridge_mint <chain> <user> <amount>`
- `bridge_burn <chain> <user> <amount>`
- `bridge_release <chain> <user> <amount>`
- `bridge_balance <chain> <user>`

## Notes
- Build defaults to release-oriented mode with tests disabled unless explicitly enabled.
- Build fails if liboqs is missing (fallback mode removed).
- Wallet key generation defaults to ML-DSA-87 via liboqs (`createwallet` returns `algo=ml-dsa-87` and stores the secret in `data/wallets/<name>.wal`). `slh-dsa-shake-256s` is opt-in and stays disabled unless this liboqs build can sign and verify with a non-empty context. This does not make the chain hash-based and is not a FIPS 140-3 claim.
- User model is Bitcoin-like (keys, UTXOs, send/receive, fee). This is not BIP-32/39/44 and not a Bitcoin fork.
- Monetary cap is enforced on-chain: `max_supply = 50,000,000`.
- Runtime strict gates enabled:
	- PQ signatures required for spend transactions (`signature` must be `pq=` format)
	- Floor fee is `min_fee=0`. Empty mempool: `recommended_min_fee=0`. Congestion can raise it. Not a Solana/XRP claim.
	- RPC LAN commands are filtered by untrusted allowlist
	- Admin-sensitive commands require trusted interface when strict admin mode is enabled
	- Local/LAN RPC token auth supported via `ADDITION_RPC_TOKEN` / `ADDITION_LAN_RPC_TOKEN`
	- Daemon refuses startup if liboqs is not linked
	- Staking requires sufficient on-chain balance
	- `sendtx` is routed through decentralized gossip path (`ok:gossiped` on success)
- Persistent state is stored under `--data-dir` (default `./data`). `blocks.dat` is written after each accepted block (not only on `quit` / SIGTERM), so a restart does not reset height to 0. Side-state (`tokens.dat`, `privacy.dat`, `staking.dat`, `contracts.dat`, `bridge.dat`, PoUW, PM) is flushed after each successful write and again on shutdown:
	- `blocks.dat` (headers + txs; UTXOs rebuilt by replay)
	- `mempool.dat`
	- `staking.dat`
	- `contracts.dat`
	- `tokens.dat`
	- `bridge.dat`
	- `peers.dat`
	- `peer_pins.dat`
	- `node_identity.dat` (stable node PQ identity)
	- `privacy.dat`
	- `wallets/<name>.wal` (local ML-DSA-87 secrets; never commit)

## Wallet (local / testnet only)
- File: `web/addition_wallet_gui.py` (Tk GUI, or `--cli` without a display)
- Wallet page: `/wallet/` via loopback `/local-rpc` → `127.0.0.1:8545`
- Uses TCP RPC on `127.0.0.1:8545` only (refuses non-loopback hosts)
- Supports:
	- Wallet creation (`createwallet [name] [scheme]`, default ML-DSA-87, key stays in `data/wallets/`)
	- Balance (`wallet_balance` / `getbalance`)
	- Default send: `wallet_send` (no raw privkey on the wire)
	- Explicit send: `tx_build` + `wallet_sign` + `sendtx_signed` / `sendtx_signed_hash`
	- Mining to the wallet address (`mine <address>`) — memory-hard, can be slow
	- Stake / unstake / claim
- Standalone CLI (keys on the caller disk): `web/addition_wallet.py` — `tx_build` + local ML-DSA-87 + `sendtx_signed_hash`. See [WALLET.md](WALLET.md).
- Not shipped in this tree: `web/addition_wallet_pro.py`, `web/portal/` (no `/api/getinfo` portal backend)
- MetaMask EVM bridge (bootstrap): `web/evm/evm_rpc_bridge.py`

## Website
- Static Pages root: `web/public/` (`/`, `/explorer/`, `/status/`, `/rpc/`, `/local/`, `/wallet/`, `/tokens/`, `/swap/`, `/privacy/`, `/docs/`, `/contracts/`, `/evm/`, `/whitepaper/`, `/legal/`)
- Local server: `python3 web/serve.py` (default `127.0.0.1:8080`)
- `/api/rpc` (and `/rpc?cmd=`) proxy the public allowlist to port `38545`
- `/rpc/` without `cmd` is the how-to page
- `/local-rpc` proxies trusted `127.0.0.1:8545` and only accepts loopback clients
- If RPC is down the pages show `RPC offline` and stay empty. They do not invent blocks, hashrate, node counts, or supply.

## MetaMask (local EVM bootstrap only)
Run:
- `python3 web/evm/evm_rpc_bridge.py`

This is **local testnet only**. Bind is `127.0.0.1:9545` (refuses `0.0.0.0`).
`eth_sendRawTransaction` is disabled. MetaMask/Trust/Binance cannot list this
as a public network.

Custom network values (Add-to-MetaMask helper on `/evm/` uses only these):
- Network Name: `ADDITION local testnet (send disabled)`
- RPC URL: `http://127.0.0.1:9545`
- Chain ID: `424242`
- Currency Symbol: `ADD`

Supported bootstrap methods:
- `web3_clientVersion`
- `eth_chainId` (`0x67932`)
- `net_version` (`424242`)
- `eth_blockNumber`
- `eth_getBlockByNumber`
- `eth_gasPrice`
- `eth_maxPriorityFeePerGas`
- `eth_getBalance` (native TEXT `getbalance`, `0x` prefix stripped)
- `eth_accounts` / `eth_requestAccounts` (empty)
- `eth_getCode` (`0x`)
- `eth_syncing` (`false`)
- `wallet_addEthereumChain` (returns the loopback params above)
- `eth_sendRawTransaction` — always disabled
- `eth_estimateGas` / `eth_call` / `eth_getTransactionCount` / `eth_feeHistory` — unsupported

Limitations:
- Not a full EVM execution node and not a public wallet RPC.
- `eth_getTransactionReceipt` / `eth_getTransactionByHash` map to native `tx_status`.
- No smart-contract bytecode execution in EVM context.
