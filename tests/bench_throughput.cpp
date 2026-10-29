// Throughput bench (phase 2): per scheme (ML-DSA-87, Falcon-1024), with and
// without the pubkey registry, fill one real block from the mempool via the
// miner under the consensus weight cap, and report included txs, weight, wire
// bytes, verify rate and implied tx/s at the configured block time.
// Keys are generated in memory only and never printed or persisted.
#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet_keys.hpp"
#include "addition/wire_v3.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using namespace addition;

static double now_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int run(const char* scheme, bool registry, int n, double block_sec) {
    auto cfg = testnet_chain_config();
    Chain chain(cfg);
    Mempool mempool;
    Miner miner(chain, mempool);
    std::string err, h;
    if (!chain.mine_and_add_block("miner1", {}, 1, h, err)) { std::cerr << err << "\n"; return 1; }

    std::vector<WalletKeys> keys;
    keys.reserve(n);
    for (int i = 0; i < n; ++i) {
        keys.push_back(generate_wallet_keys(scheme));
        if (!chain.credit_balance(keys.back().address, 1000, "bench_seed", err)) { std::cerr << err << "\n"; return 1; }
    }
    auto make = [&](const WalletKeys& k) {
        Transaction tx{};
        if (!chain.build_transaction(k.address, "bob", 10, 1, chain.next_nonce(k.address), tx, err)) return tx;
        tx.signer = k.address;
        tx.signer_pubkey = chain.pubkey_field_for(k.address, k.public_key);
        tx.signature = sign_message_hybrid(k.private_key, hash_transaction(tx));
        return tx;
    };
    if (registry) {  // one registering tx per sender, mined in weight-bounded batches
        std::vector<Transaction> batch;
        std::size_t w = 0;
        for (const auto& k : keys) {
            auto tx = make(k);
            if (w + tx_weight(tx) > cfg.max_block_weight - 8192) {
                if (!chain.mine_and_add_block("miner1", batch, 1, h, err)) { std::cerr << err << "\n"; return 1; }
                batch.clear(); w = 0;
                tx = make(k);
            }
            w += tx_weight(tx);
            batch.push_back(tx);
        }
        if (!batch.empty() && !chain.mine_and_add_block("miner1", batch, 1, h, err)) { std::cerr << err << "\n"; return 1; }
    }
    std::vector<Transaction> txs;
    for (const auto& k : keys) txs.push_back(make(k));

    sig_cache_set_enabled(false);
    double a = now_ms();
    for (auto& tx : txs) if (!chain.validate_transaction(tx, err)) { std::cerr << err << "\n"; return 1; }
    const double verify_tps = n * 1000.0 / (now_ms() - a);
    sig_cache_set_enabled(true);
    sig_cache_clear();

    Chain replica = chain;  // pre-tip state (incl. bench credits) for a cold peer-side check
    for (auto& tx : txs) mempool.submit(tx);
    a = now_ms();
    if (!miner.mine_next_block("miner1", 100000, 1, h, err)) { std::cerr << err << "\n"; return 1; }
    const double mine_ms = now_ms() - a;
    const Block& tip = chain.tip();
    const std::size_t included = tip.transactions.size() - 1;  // minus coinbase
    // Cold re-validation of the full block on a fresh replica (what a peer pays).
    sig_cache_clear();
    a = now_ms();
    if (!replica.add_block(tip, err)) { std::cerr << "peer add_block: " << err << "\n"; return 1; }
    const double replay_ms = now_ms() - a;

    std::cout << scheme << (registry ? " registry " : " +pubkey  ")
              << "tx_weight=" << tx_weight(txs[0]) << " tx_wire_b64=" << tx_wire_bytes(txs[0])
              << " | block: included=" << included << "/" << n
              << " weight=" << block_weight(tip) << "/" << cfg.max_block_weight
              << " wire_b64=" << (4 + wire::base64_encode(wire::encode_block_v3(tip)).size())
              << " | verify=" << static_cast<int>(verify_tps) << " tx/s"
              << " mine+validate=" << static_cast<int>(mine_ms) << "ms"
              << " peer cold add_block=" << static_cast<int>(replay_ms) << "ms"
              << " => " << included / block_sec << " tx/s @" << block_sec << "s\n";
    return 0;
}

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::stoi(argv[1]) : 1500;
    const double block_sec = 20.0;
    int rc = 0;
    rc |= run("ml-dsa-87", false, n, block_sec);
    rc |= run("ml-dsa-87", true, n, block_sec);
    rc |= run("fn-dsa-1024-v1", false, n, block_sec);
    rc |= run("fn-dsa-1024-v1", true, n, block_sec);
    return rc;
}
