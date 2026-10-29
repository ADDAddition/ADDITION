// Throughput bench: ML-DSA-87 verify rate (cache off/on), tx wire size, and
// how many txs the relay byte budget lets into one mined block.
// Keys are generated in memory only and never printed.
#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet_keys.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::stoi(argv[1]) : 40;
    addition::Chain chain(addition::testnet_chain_config());
    addition::Mempool mempool;
    addition::Miner miner(chain, mempool);
    std::string err, h;
    if (!miner.mine_next_block("miner1", 200, 1, h, err)) { std::cerr << err << "\n"; return 1; }

    std::vector<addition::Transaction> txs;
    for (int i = 0; i < n; ++i) {
        auto k = addition::generate_wallet_keys();
        if (!chain.credit_balance(k.address, 100, "bench_seed", err)) { std::cerr << err << "\n"; return 1; }
        addition::Transaction tx{};
        if (!chain.build_transaction(k.address, "bob", 10, 1, 1, tx, err)) { std::cerr << err << "\n"; return 1; }
        tx.signer = k.address;
        tx.signer_pubkey = k.public_key;
        auto u = tx; u.signature.clear();
        tx.signature = addition::sign_message_hybrid(k.private_key, addition::hash_transaction(u));
        txs.push_back(tx);
    }
    auto t = [] { return std::chrono::steady_clock::now(); };
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };

    addition::sig_cache_set_enabled(false);
    auto a = t();
    for (auto& tx : txs) { if (!chain.validate_transaction(tx, err)) { std::cerr << err << "\n"; return 1; } }
    auto b = t();
    std::cout << "validate_transaction no-cache: " << n * 1000.0 / ms(a, b) << " tx/s\n";
    addition::sig_cache_set_enabled(true);
    addition::sig_cache_clear();
    for (auto& tx : txs) chain.validate_transaction(tx, err);
    a = t();
    for (auto& tx : txs) chain.validate_transaction(tx, err);
    b = t();
    std::cout << "validate_transaction cached:   " << n * 1000.0 / ms(a, b) << " tx/s\n";
    std::cout << "tx wire bytes (BLKDATA): " << addition::tx_wire_bytes(txs[0]) << "\n";

    addition::sig_cache_clear();
    for (auto& tx : txs) mempool.submit(tx);
    a = t();
    if (!miner.mine_next_block("miner1", 200, 1, h, err)) { std::cerr << err << "\n"; return 1; }
    b = t();
    const auto deferred = miner.last_deferred_for_size();
    std::cout << "mined block: included=" << (n - deferred) << " deferred=" << deferred
              << " est_wire_bytes=" << miner.last_block_wire_bytes()
              << " (limit " << addition::kMaxRelayBlockWireBytes << ") mine+validate_ms=" << ms(a, b) << "\n";
    std::cout << "=> sustained capacity at 60s blocks: " << (n - deferred) / 60.0 << " tx/s\n";
    std::cout << "mempool left: " << mempool.size() << "\n";
    return 0;
}
