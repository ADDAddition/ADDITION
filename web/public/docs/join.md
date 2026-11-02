# Run an ADDITION node (testnet)

ADDITION mainnet has NOT launched. These steps join the research testnet.

1. Dependencies: C++20 compiler, CMake, Ninja, OpenSSL, liboqs 0.12.0, Tor (Rust for the shielded pool).
2. Build:

       git clone https://github.com/ADDAddition/ADDITION && cd ADDITION
       cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/oqs
       ninja -C build additiond

3. Run over Tor:

       ./build/additiond --network testnet --onion-only --tor-proxy 127.0.0.1:9050 --bootstrap <peer>.onion:PORT

4. Local RPC (127.0.0.1:8545, never expose it): `createwallet <name>`, `getinfo`, `mine <addr>`.

Mainnet seeds will be Tor onion addresses in seeds-mainnet.txt (empty until published). See docs/ONION_SEEDS.md.
