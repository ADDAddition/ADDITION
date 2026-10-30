# Mainnet onion seeds

Mainnet (`ADDITION_MAINNET_V1`) no longer has a clearnet default seed:
`<seed>.onion:28546` was removed as the default, and so was HTTP read/ingest.

`include/addition/config.hpp` → `kMainnetOnionSeeds[]` lists the seed onions.
**They are placeholders for now** (`placeholdermainnetseed…onion:28546`). They are
well-formed v3 onions, but `is_placeholder_seed()` filters them out, so they are
never dialed. While only placeholders exist, `--mainnet` has an empty default
seed list. additiond prints a warning, and the operator joins with:

    additiond --mainnet --tor-proxy 127.0.0.1:9050 \
      --bootstrap <real-seed>.onion:28546 [--onion-only]

## Publishing the real seeds (owner action, not done here)
1. On each seed host, create a Tor v3 hidden service (`HiddenServicePort 28546 127.0.0.1:28546`).
2. Run `additiond --mainnet --onion-only --p2p-bind 127.0.0.1` and set
   `ADDITION_ADVERTISED_P2P=<onion>:28546`.
3. Replace the placeholder entries in `kMainnetOnionSeeds[]`, then rebuild.
   `test_mainnet_chain` checks that defaults contain only real onion seeds.

Mobile clients have no hosted/public read URL. They read only from a
user-run node (loopback or the user's own `.onion`).
