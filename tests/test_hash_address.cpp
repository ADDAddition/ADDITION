#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet.hpp"
#include "addition/wallet_keys.hpp"

#include <cctype>
#include <iostream>
#include <string>

namespace {

bool is_128_hex(const std::string& value) {
    if (value.size() != addition::kHashCommittedAddressHexLen) {
        return false;
    }
    for (char c : value) {
        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    addition::WalletKeys keys{};
    try {
        keys = addition::generate_wallet_keys();
    } catch (const std::exception& e) {
        std::cerr << "test failed: keygen: " << e.what() << '\n';
        return 1;
    }

    if (!is_128_hex(keys.address)) {
        std::cerr << "test failed: address must be 128 hex, got size=" << keys.address.size() << '\n';
        return 1;
    }
    if (keys.public_key.size() != 2592 * 2) {
        std::cerr << "test failed: ML-DSA-87 pubkey size\n";
        return 1;
    }
    if (keys.address == keys.public_key || keys.address.size() == keys.public_key.size()) {
        std::cerr << "test failed: address must not be the raw ML-DSA-87 key\n";
        return 1;
    }
    if (keys.algorithm != addition::kMlDsa87SchemeId) {
        std::cerr << "test failed: algorithm must be ml-dsa-87\n";
        return 1;
    }

    const auto derived = addition::hash_committed_address_hex(addition::kMlDsa87SchemeId, keys.public_key);
    if (derived != keys.address) {
        std::cerr << "test failed: wallet address must match SHA3-512(scheme||0x00||pk)\n";
        return 1;
    }
    const auto other_scheme = addition::hash_committed_address_hex("slh-dsa-shake-256s", keys.public_key);
    if (other_scheme.size() != addition::kHashCommittedAddressHexLen || other_scheme == keys.address) {
        std::cerr << "test failed: scheme_id must change the address preimage\n";
        return 1;
    }

    std::string bind_err;
    if (!addition::address_binds_pubkey(keys.address, addition::kMlDsa87SchemeId, keys.public_key, bind_err)) {
        std::cerr << "test failed: bind good pubkey: " << bind_err << '\n';
        return 1;
    }
    const auto garbage_pk = std::string(keys.public_key.size(), '0');
    if (addition::address_binds_pubkey(keys.address, addition::kMlDsa87SchemeId, garbage_pk, bind_err)) {
        std::cerr << "test failed: garbage pubkey must fail hash-address bind\n";
        return 1;
    }
    if (bind_err.find("mismatch") == std::string::npos) {
        std::cerr << "test failed: expected mismatch error, got: " << bind_err << '\n';
        return 1;
    }

    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    addition::Chain chain(easy);

    std::string error;
    if (!chain.credit_balance(keys.address, 100, "hash_addr_seed", error)) {
        std::cerr << "test failed: credit: " << error << '\n';
        return 1;
    }

    addition::Transaction forged{};
    if (!chain.build_transaction(keys.address, "eve", 5, 1, chain.next_nonce(keys.address), forged, error)) {
        std::cerr << "test failed: build forged: " << error << '\n';
        return 1;
    }
    forged.signer = keys.address;
    forged.signer_pubkey = garbage_pk;
    forged.signature = "pq=" + std::string(16, '0');
    if (chain.validate_transaction(forged, error)) {
        std::cerr << "test failed: garbage pubkey spend must be rejected\n";
        return 1;
    }
    if (error.find("hash-address") == std::string::npos && error.find("mismatch") == std::string::npos) {
        std::cerr << "test failed: spend reject reason: " << error << '\n';
        return 1;
    }

    addition::Mempool mempool;
    addition::Wallet wallet(keys.address, keys.public_key, keys.private_key);
    if (!wallet.send(mempool, chain, "bob", 10, 1, error)) {
        std::cerr << "test failed: send: " << error << '\n';
        return 1;
    }
    addition::Miner miner(chain, mempool);
    std::string mined;
    if (!miner.mine_next_block(keys.address, 200, 1, mined, error)) {
        std::cerr << "test failed: mine: " << error << '\n';
        return 1;
    }
    if (chain.balance_of("bob") != 10) {
        std::cerr << "test failed: bob balance after hash-address spend\n";
        return 1;
    }

    std::cout << "hash-committed address tests passed\n";
    return 0;
}
