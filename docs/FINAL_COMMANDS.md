# ADDITION_FINAL - Final Command Surface

## RPC Endpoints
- Local-only RPC: `127.0.0.1:8545`
- LAN RPC: `0.0.0.0:18545`
- P2P transport RPC: `0.0.0.0:28545`

Each TCP request is one command line and returns one response line.

## Core chain
- `getinfo`
- `monetary_info`
- `crypto_selftest`
- `createwallet`
- `getbalance <address>`
- `getbalance_instant <address>`
- `tx_build <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce>`
- `sendtx_signed <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>`
- `sendtx_signed_hash <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>`
- `sendtx <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce>` (legacy, disabled unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`)
- `sendtx_hash <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce>` (legacy, disabled unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`)
- `tx_status <tx_hash>`
- `mine`

## P2P + Consensus
- `addpeer <ip:port>`
- `delpeer <ip:port>`
- `peers`
- `vote <peer> <height> <block_hash>`
- `quorum <height> <block_hash>`
- `peer_inbound <peer> <payload>`
- `gossip_flush`
- `sync`
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
	- request: `HELLO|2|ADDITION_MAINNET_V1|<unix_ts>|<nonce>|<peer_pubkey>|<peer_signature>`
	- response: `HELLO_ACK|2|ADDITION_MAINNET_V1|<unix_ts>|<echo_nonce>|<responder_pubkey>|<responder_signature>`
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
- `privacy_native_verifier <pq_mldsa87>`
- `privacy_mint_zk <owner> <amount> <commitment_hex> <nullifier_hex> <proof_hex> <vk_hex>`
- `privacy_spend_zk <owner> <note_id> <recipient> <amount> <nullifier_hex> <proof_hex> <vk_hex>`

Strict privacy mode notes:
- `privacy_mint_zk` / `privacy_spend_zk` are verified only in-process via native mode `pq_mldsa87`.
- Non-ZK privacy commands are removed; privacy flow is ZK-only.
- Strict ZK privacy policy is always ON (runtime toggle removed).
- External verifier wrappers are disabled in mainnet native-only mode.
- Proof and verification key are mandatory; no internal simulation verifier is used.
- Note storage hardening: `owner` and `amount` are not persisted in plaintext.
- Required for private note operations: set `ADDITION_PRIVACY_MASTER_KEY` (minimum 32 chars) before node startup.
- `owner_tag` derivation is also keyed with `ADDITION_PRIVACY_MASTER_KEY` to avoid cross-dataset linkage from state files.
- Private balance query commands are removed from command surface to reduce metadata leakage.

Verifier tooling:
- Native verifier is integrated in C++ (`privacy.cpp`) and uses ML-DSA-87 verification primitives.

## Staking
- `stake <address> <amount>`
- `unstake <address> <amount>`
- `staked <address>`
- `stake_reward <amount>`
- `stake_claim <address>`

## Smart-contract runtime
- `contract_deploy <owner> <code>`
- `contract_call <id> <set|add|get|token_balance|swap_quote|zk_mint|zk_spend|zk_privacy_status> <key> <value>`

### ZK privacy methods via `contract_call`
- `zk_mint`
	- key format: `<OWNER>:<COMMITMENT_HEX>:<NULLIFIER_HEX>:<PROOF_HEX>:<VK_HEX>`
	- value: amount to mint privately (`> 0`)
	- return: created `note_id`
- `zk_spend`
	- key format: `<OWNER>:<NOTE_ID>:<RECIPIENT>:<NULLIFIER_HEX>:<PROOF_HEX>:<VK_HEX>`
	- value: private amount to send (`> 0`)
	- return: recipient new private `note_id`
- `zk_privacy_status`
	- key format: any non-empty token (for example `status`)
	- value: ignored (set `0`)
	- return: `verifier_configured`, `strict_zk_mode`, notes and nullifier stats

Notes:
- For strongest privacy, keep `strict_zk_mode` ON.
- `zk_mint` and `zk_spend` require valid proof/vk hex and native in-process ML-DSA-87 verification.

## Token & NFT runtime
- `token_create <symbol> <owner> <max_supply> <initial_mint>`
- `token_mint <symbol> <caller> <to> <amount>`
- `token_transfer <symbol> <from> <to> <amount>`
- `token_balance <symbol> <owner>`
- `nft_mint <collection> <token_id> <owner> <metadata>`
- `nft_transfer <collection> <token_id> <from> <to>`
- `nft_owner <collection> <token_id>`

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
- Wallet key generation uses ML-DSA-87 via liboqs when available (`createwallet` returns `algo=ml-dsa-87`).
- Monetary cap is enforced on-chain: `max_supply = 50,000,000`.
- Runtime strict gates enabled:
	- PQ signatures required for spend transactions (`signature` must be `pq=` format)
	- Minimum transaction fee enforced (`min_fee=1`)
	- RPC LAN commands are filtered by untrusted allowlist
	- Admin-sensitive commands require trusted interface when strict admin mode is enabled
	- Local/LAN RPC token auth supported via `ADDITION_RPC_TOKEN` / `ADDITION_LAN_RPC_TOKEN`
	- Daemon refuses startup if liboqs is not linked
	- Staking requires sufficient on-chain balance
	- `sendtx` is routed through decentralized gossip path (`ok:gossiped` on success)
- Persistent state is stored in `./data` on daemon shutdown and restored on startup:
	- `blocks.dat`
	- `mempool.dat`
	- `staking.dat`
	- `contracts.dat`
	- `tokens.dat`
	- `bridge.dat`
	- `peers.dat`
	- `peer_pins.dat`
	- `node_identity.dat` (stable node PQ identity)
	- `privacy.dat`

## Wallet GUI (Python)
- File: `web/addition_wallet_gui.py`
- Uses TCP RPC directly on `127.0.0.1:8545`
- Supports:
	- Wallet creation (`createwallet`)
	- Balance refresh (`getbalance`)
	- Secure send flow (`tx_build` + `sign_message` + `sendtx_signed_hash`)
	- Mining to selected address (`mine <address>`)
	- Stake / unstake / claim

## Professional UX Components
- Advanced wallet UI: `web/addition_wallet_pro.py`
- Live web portal: `web/portal/index.html`
- Portal metrics backend: `web/portal/addition_portal_backend.py`
- MetaMask EVM bridge (bootstrap): `web/evm/evm_rpc_bridge.py`

Portal API endpoints:
- `GET /api/getinfo`
- `GET /api/monetary`
- `GET /api/health`

## MetaMask (EVM bridge bootstrap)
Run:
- `python web/evm/evm_rpc_bridge.py`

Custom network values:
- Network Name: `Addition EVM Bridge`
- RPC URL: `http://127.0.0.1:9545`
- Chain ID: `424242`
- Currency Symbol: `ADD`
- Block Explorer URL: `http://127.0.0.1:8080`

Supported bootstrap methods:
- `web3_clientVersion`
- `eth_chainId`
- `net_version`
- `eth_blockNumber`
- `eth_getBlockByNumber`
- `eth_gasPrice`
- `eth_maxPriorityFeePerGas`
- `eth_feeHistory`
- `eth_getBalance`
- `eth_accounts`
- `eth_requestAccounts`
- `eth_estimateGas`
- `eth_getTransactionCount`
- `eth_sendRawTransaction` (strict mode: disabled until native execution mapping)
- `eth_getTransactionReceipt`
- `eth_getTransactionByHash`
- `eth_getCode`
- `eth_call`
- `eth_syncing`
- `wallet_addEthereumChain`
- `wallet_switchEthereumChain`

Limitations (current bridge stage):
- Not a full EVM execution node.
- `eth_sendRawTransaction` is disabled in secure mode.
- `eth_getTransactionReceipt` / `eth_getTransactionByHash` map to native `tx_status`.
- No smart-contract bytecode execution in EVM context yet.
