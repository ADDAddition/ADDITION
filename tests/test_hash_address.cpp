#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet.hpp"
#include "addition/wallet_keys.hpp"

#include <iostream>
#include <string>

int main() {
    std::string selftest;
    if (!addition::crypto_selftest(selftest)) {
        std::cerr << "test failed: crypto_selftest: " << selftest << '\n';
        return 1;
    }
    if (selftest.find("scheme=ml-dsa-87") == std::string::npos ||
        selftest.find("pk_bytes=") == std::string::npos ||
        selftest.find("sig_bytes=") == std::string::npos ||
        selftest.find("sha3_512=") == std::string::npos ||
        selftest.find("pq_mode=strict") == std::string::npos ||
        selftest.find("liboqs=") == std::string::npos ||
        selftest.find("openssl=") == std::string::npos ||
        selftest.find("sign_verify=ok") == std::string::npos ||
        selftest.find("empty_ctx_rejected=1") == std::string::npos ||
        selftest.find("tps") != std::string::npos) {
        std::cerr << "test failed: selftest fields: " << selftest << '\n';
        return 1;
    }

    addition::WalletKeys keys{};
    try {
        keys = addition::generate_wallet_keys("ml-dsa-87");
    } catch (const std::exception& e) {
        std::cerr << "test failed: keygen: " << e.what() << '\n';
        return 1;
    }
    if (keys.address.size() != 128 || keys.public_key.size() == keys.address.size()) {
        std::cerr << "test failed: address must be 128-hex SHA3-512, not the raw key\n";
        return 1;
    }
    if (keys.public_key.size() != 2592 * 2) {
        std::cerr << "test failed: ML-DSA-87 pubkey size\n";
        return 1;
    }
    if (keys.address == keys.public_key) {
        std::cerr << "test failed: address equals raw pubkey\n";
        return 1;
    }

    std::string bind_err;
    if (!addition::address_binds_pubkey(keys.address, addition::SigScheme::MlDsa87, keys.public_key, bind_err)) {
        std::cerr << "test failed: bind good pubkey: " << bind_err << '\n';
        return 1;
    }
    const auto garbage_pk = std::string(keys.public_key.size(), '0');
    if (addition::address_binds_pubkey(keys.address, addition::SigScheme::MlDsa87, garbage_pk, bind_err)) {
        std::cerr << "test failed: garbage pubkey must fail hash-address bind\n";
        return 1;
    }

    addition::ChainConfig easy = addition::regtest_chain_config();
    addition::Chain chain(easy);
    const auto ctx = chain.consensus_sign_context();
    if (ctx.empty() || ctx.size() > 255 || ctx.rfind("ADDITION|", 0) != 0) {
        std::cerr << "test failed: consensus ctx: " << ctx << '\n';
        return 1;
    }

    const std::string msg = "ctx-bind-message";
    std::string sig;
    try {
        sig = addition::sign_message_hybrid(keys.private_key, msg, ctx, "ml-dsa-87");
    } catch (const std::exception& e) {
        std::cerr << "test failed: sign: " << e.what() << '\n';
        return 1;
    }
    if (!addition::verify_message_signature_hybrid(keys.public_key, msg, sig, ctx, "ml-dsa-87")) {
        std::cerr << "test failed: verify with correct ctx\n";
        return 1;
    }
    if (addition::verify_message_signature_hybrid(keys.public_key, msg, sig, "", "ml-dsa-87")) {
        std::cerr << "test failed: empty ctx must fail verify\n";
        return 1;
    }
    if (addition::verify_message_signature_hybrid(keys.public_key, msg, sig, "ADDITION|wrong|ctx", "ml-dsa-87")) {
        std::cerr << "test failed: wrong ctx must fail verify\n";
        return 1;
    }

    std::string error;
    if (!chain.credit_balance(keys.address, 100, "hash_addr_seed", error)) {
        std::cerr << "test failed: credit: " << error << '\n';
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

    addition::Transaction bad = mempool.snapshot().empty() ? addition::Transaction{} : addition::Transaction{};
    (void)bad;
    addition::Transaction forged{};
    if (!chain.build_transaction(keys.address, "eve", 5, 1, chain.next_nonce(keys.address), forged, error)) {
        std::cerr << "test failed: build forged: " << error << '\n';
        return 1;
    }
    forged.signer = keys.address;
    forged.signer_pubkey = garbage_pk;
    forged.signer_scheme = "ml-dsa-87";
    forged.signature = sig;
    if (chain.validate_transaction(forged, error)) {
        std::cerr << "test failed: garbage pubkey spend must be rejected\n";
        return 1;
    }

    std::cout << "hash-address and ML-DSA context tests passed\n";
    return 0;
}
