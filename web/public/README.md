# ADDITION static site

Pages root for additionblockchain.com.

Look from the March 7 `web/portal` dump: Inter + JetBrains Mono, dark topbar, KPI tiles, panel cards, fade-up, orb background. Copy is what the node does.

- Home, Network (`getinfo` + `getblock`), Node (`additiond --network testnet`), About.
- KPI tiles: `height`, `peers`, `tps` only if getinfo has `tps` or `last_tps`, health. Down node → **RPC offline**, empty cells.
- Explorer, Status, RPC, Docs, Wallet, Legal stay linked from the footer.
- Worker: `worker.js` / `_worker.js`. `PUBLIC_RPC_HTTP` empty by default.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
