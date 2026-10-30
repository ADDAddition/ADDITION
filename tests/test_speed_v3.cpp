// Phase-2 speed work: wire v3, block weight, pubkey registry (ML-DSA-87 only),
// shielded fit, inbound connection caps. Test keys are generated in memory and
// never printed or persisted.
#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/net_io.hpp"
#include "addition/rpc_network_server.hpp"
#include "addition/wallet_keys.hpp"
#include "addition/wire_v3.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::cerr << "FAIL " << __LINE__ << ": " #c << "\n";    \
            ++failures;                                             \
        }                                                           \
    } while (0)

using namespace addition;

void test_tagged_roundtrip() {
    const std::vector<std::string> cases = {"", "abc", "00ff", "00FF", "0", "pq=", "pq=0a", "pq=0A", "pq=abc",
                                            std::string(1, '\0') + "x", "deadbeef", "miner1"};
    for (const auto& c : cases) {
        std::string b;
        wire::write_tagged(b, c);
        std::size_t off = 0;
        std::string back;
        CHECK(wire::read_tagged(b, off, back) && back == c && off == b.size());
    }
    std::string bin;
    for (int i = 0; i < 256; ++i) bin.push_back(static_cast<char>(i));
    for (std::size_t n = 0; n < 7; ++n) {
        std::string out;
        CHECK(wire::base64_decode(wire::base64_encode(bin.substr(0, n)), out) && out == bin.substr(0, n));
    }
    std::string out;
    CHECK(!wire::base64_decode("ab=c", out));
    CHECK(!wire::base64_decode("abc", out));
}

Transaction signed_tx(const WalletKeys& k, bool with_pub, std::uint64_t nonce) {
    Transaction tx{};
    tx.signer = k.address;
    tx.signer_pubkey = with_pub ? k.public_key : "";
    tx.fee = 1;
    tx.nonce = nonce;
    tx.inputs.push_back({std::string(128, 'c'), 0});
    tx.outputs.push_back({k.address, 5});
    tx.signature = sign_message_hybrid(k.private_key, hash_transaction(tx));
    return tx;
}

void test_wire_and_weight(const WalletKeys& mldsa, const WalletKeys& mldsa2) {
    for (const auto* k : {&mldsa, &mldsa2}) {
        for (bool pub : {true, false}) {
            const auto tx = signed_tx(*k, pub, 7);
            const auto bin = wire::encode_tx_v3(tx);
            Transaction back;
            std::string err;
            CHECK(wire::decode_tx_v3(bin, back, err));
            CHECK(hash_transaction(back) == hash_transaction(tx) && back.signature == tx.signature &&
                  back.signer_pubkey == tx.signer_pubkey);
            std::string corrupt = bin;
            corrupt[corrupt.size() / 2] ^= 1;
            CHECK(!wire::decode_tx_v3(corrupt, back, err));
            std::cout << k->algorithm << (pub ? " +pubkey" : " registry") << " tx_weight=" << tx_weight(tx)
                      << " wire_b64=" << tx_wire_bytes(tx) << "\n";
        }
    }
    Block b{};
    b.header.height = 1;
    b.header.previous_hash = std::string(128, 'a');
    b.header.merkle_root = std::string(128, 'b');
    b.transactions.push_back(signed_tx(mldsa, true, 1));
    b.transactions.push_back(signed_tx(mldsa2, false, 2));
    const auto bb = wire::encode_block_v3(b);
    Block bk;
    std::string err;
    CHECK(wire::decode_block_v3(bb, bk, err) && hash_block_header(bk.header) == hash_block_header(b.header) &&
          bk.transactions.size() == 2 && hash_transaction(bk.transactions[1]) == hash_transaction(b.transactions[1]));
    CHECK(block_weight(b) == bb.size());  // weight == canonical binary size
    // Truncated / hostile counts rejected without allocation blowup.
    std::string hostile = "TXV3";
    CHECK(!wire::decode_tx_v3(hostile + std::string(4, '\xff'), bk.transactions[0], err));
}

void test_registry_and_reorg(const WalletKeys& k) {
    auto cfg = testnet_chain_config();
    Chain a(cfg);
    std::string h, err;
    CHECK(a.mine_and_add_block(k.address, {}, 2, h, err));
    CHECK(a.mine_and_add_block(k.address, {}, 2, h, err));
    // Unregistered: a key-less tx is rejected.
    Transaction t0;
    CHECK(a.build_transaction(k.address, "dest1", 3, 1, a.next_nonce(k.address), t0, err));
    t0.signer = k.address;
    t0.signer_pubkey.clear();
    t0.signature = sign_message_hybrid(k.private_key, hash_transaction(t0));
    CHECK(!a.validate_transaction(t0, err));
    CHECK(a.pubkey_field_for(k.address, k.public_key) == k.public_key);
    // Register with first spend.
    Transaction t1;
    CHECK(a.build_transaction(k.address, "dest1", 3, 1, a.next_nonce(k.address), t1, err));
    t1.signer = k.address;
    t1.signer_pubkey = a.pubkey_field_for(k.address, k.public_key);
    t1.signature = sign_message_hybrid(k.private_key, hash_transaction(t1));
    CHECK(a.validate_transaction(t1, err));
    CHECK(a.mine_and_add_block(k.address, {t1}, 2, h, err));
    std::string reg;
    CHECK(a.registered_pubkey(k.address, reg) && reg == k.public_key);
    CHECK(a.pubkey_field_for(k.address, k.public_key).empty());
    // Key-less tx now valid and mineable.
    Transaction t2;
    CHECK(a.build_transaction(k.address, "dest2", 2, 1, a.next_nonce(k.address), t2, err));
    t2.signer = k.address;
    t2.signer_pubkey = a.pubkey_field_for(k.address, k.public_key);
    t2.signature = sign_message_hybrid(k.private_key, hash_transaction(t2));
    CHECK(t2.signer_pubkey.empty());
    CHECK(a.validate_transaction(t2, err));
    CHECK(a.mine_and_add_block(k.address, {t2}, 2, h, err));
    // A forged key-less tx (wrong signer key) fails.
    auto other = generate_wallet_keys("ml-dsa-87");
    Transaction t3 = t2;
    t3.nonce += 1;
    t3.signature = sign_message_hybrid(other.private_key, hash_transaction(t3));
    CHECK(!a.validate_transaction(t3, err));
    // Persist/replay: rebuilding from blocks reproduces the registry.
    Chain replay(cfg);
    CHECK(replay.replace_with_chain(a.blocks(), err));
    CHECK(replay.registered_pubkey(k.address, reg));
    // Reorg to a heavier chain without the registration => entry gone.
    Chain b(cfg);
    for (int i = 0; i < 6; ++i) CHECK(b.mine_and_add_block("miner-b", {}, 2, h, err));
    CHECK(a.replace_with_chain(b.blocks(), err));
    CHECK(!a.registered_pubkey(k.address, reg));
    CHECK(a.registry_size() == 0);
}

void test_block_weight_rule(const WalletKeys& k) {
    auto cfg = testnet_chain_config();
    cfg.max_block_weight = 65536 + 1;  // tiny cap for the test
    Chain c(cfg);
    std::string h, err;
    CHECK(c.mine_and_add_block(k.address, {}, 2, h, err));
    // Oversized block via a fat (non-consensus-valid but weight-heavy) coinbase output name.
    Block blk = c.tip();
    blk.header.height += 1;
    blk.header.previous_hash = hash_block_header(c.tip().header);
    blk.transactions.clear();
    Transaction cb{};
    cb.outputs.push_back({std::string(70000, 'z'), 1});
    blk.transactions.push_back(cb);
    blk.header.merkle_root = compute_merkle_root(blk.transactions);
    CHECK(block_weight(blk) > cfg.max_block_weight);
    CHECK(!c.add_block(blk, err));
    CHECK(err.find("weight") != std::string::npos);
}

void test_shielded_fit(const WalletKeys& k) {
    Transaction tx = signed_tx(k, false, 1);
    tx.shielded = std::string(228 * 1024 * 2, 'a');  // ~228 KB bundle (hex)
    const auto w = tx_weight(tx);
    std::cout << "shielded 228KB tx weight=" << w << " wire_b64=" << tx_wire_bytes(tx)
              << " per_block=" << (kMaxBlockWeight / w) << "\n";
    CHECK(w < 240 * 1024);
    CHECK(tx_wire_bytes(tx) < kMaxLineBytes);
    CHECK(kMaxBlockWeight / w >= 8);
    Transaction back;
    std::string err;
    CHECK(wire::decode_tx_v3(wire::encode_tx_v3(tx), back, err) && back.shielded == tx.shielded);
    // Not validated on this branch => consensus rejects it (fail closed).
    Chain c(testnet_chain_config());
    CHECK(!c.validate_transaction(tx, err));
}

int connect_local(std::uint16_t port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return -1;
    }
    return s;
}

void test_connection_caps() {
    const std::uint16_t port = 39871;
    RpcNetworkServer srv("127.0.0.1", port, [](const std::string& req) { return "ok:" + std::to_string(req.size()); });
    std::string err;
    CHECK(srv.start(err));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<int> socks;
    for (std::size_t i = 0; i < kMaxInboundLoopback + 4; ++i) {
        socks.push_back(connect_local(port));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto st = srv.stats();
    CHECK(st.active == kMaxInboundLoopback);
    CHECK(st.rejected_ip_cap >= 4);
    CHECK(st.large_slots_in_use <= kMaxLargeSlots);
    for (int s : socks) {
        if (s >= 0) ::close(s);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(srv.stats().active == 0);
    // A >256 KiB request still works with a free large slot (4 MiB line).
    int s = connect_local(port);
    std::string big(1024 * 1024, 'q');
    big.push_back('\n');
    CHECK(socket_send_all(static_cast<std::uintptr_t>(s), big.data(), big.size()));
    std::string line;
    CHECK(socket_recv_line(static_cast<std::uintptr_t>(s), line, 1024));
    CHECK(line.rfind("ok:1048576", 0) == 0);
    ::close(s);
    srv.stop();
}
} // namespace

int main() {
    const auto mldsa = generate_wallet_keys("ml-dsa-87");
    const auto mldsa2 = generate_wallet_keys("ml-dsa-87");
    SigScheme rej{};
    if (parse_sig_scheme("fn-dsa-1024-v1", rej) || parse_sig_scheme("falcon-1024", rej)) { std::cerr << "FAIL falcon still parses\n"; ++failures; }
    test_tagged_roundtrip();
    test_wire_and_weight(mldsa, mldsa2);
    test_registry_and_reorg(mldsa);
    test_registry_and_reorg(mldsa2);
    test_block_weight_rule(mldsa);
    test_shielded_fit(mldsa);
    test_connection_caps();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "test_speed_v3 ok\n";
    return 0;
}
