#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet.hpp"
#include "addition/wallet_keys.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main() {
    if (!addition::sig_scheme_allowed_strict(addition::SigScheme::MlDsa87)) {
        std::cerr << "test failed: ML-DSA-87 must remain available\n";
        return 1;
    }

    const auto algs = addition::allowed_sig_algs_list();
    if (algs.find("ml-dsa-87") == std::string::npos) {
        std::cerr << "test failed: allowed_sig_algs missing ml-dsa-87: " << algs << '\n';
        return 1;
    }

    addition::SigScheme unknown = addition::SigScheme::Unknown;
    if (addition::parse_sig_scheme("falcon-512", unknown) ||
        addition::sig_scheme_allowed_strict(addition::SigScheme::Unknown)) {
        std::cerr << "test failed: unknown scheme must be rejected in strict mode\n";
        return 1;
    }
    try {
        (void)addition::generate_wallet_keys("fn-dsa");
        std::cerr << "test failed: unknown createwallet scheme must throw\n";
        return 1;
    } catch (const std::exception&) {
    }

    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    addition::Chain chain(easy);
    addition::Mempool mempool;
    addition::Miner miner(chain, mempool);
    std::string error;

    addition::WalletKeys ml = addition::generate_wallet_keys("ml-dsa-87");
    if (!chain.credit_balance(ml.address, 40, "ml_seed", error)) {
        std::cerr << "test failed: credit ml: " << error << '\n';
        return 1;
    }
    addition::Wallet ml_wallet(ml.address, ml.public_key, ml.private_key);
    if (!ml_wallet.send(mempool, chain, "ml-recv", 5, 1, error)) {
        std::cerr << "test failed: ML-DSA send: " << error << '\n';
        return 1;
    }
    std::string mined;
    if (!miner.mine_next_block("miner", 200, 1, mined, error)) {
        std::cerr << "test failed: mine ml: " << error << '\n';
        return 1;
    }
    if (chain.balance_of("ml-recv") != 5) {
        std::cerr << "test failed: ML-DSA-87 path must still work\n";
        return 1;
    }

    {
        const std::vector<std::uint8_t> dummy(64, 1);
        const auto a_ml = addition::hash_committed_address(addition::SigScheme::MlDsa87, dummy);
        const auto a_slh = addition::hash_committed_address(addition::SigScheme::SlhDsaShake256s, dummy);
        if (a_ml.empty() || a_slh.empty() || a_ml == a_slh) {
            std::cerr << "test failed: scheme_id must change the address hash\n";
            return 1;
        }
        if (a_ml.size() != addition::kHashCommittedAddressHexLen ||
            a_slh.size() != addition::kHashCommittedAddressHexLen) {
            std::cerr << "test failed: dummy hashes must be 128 hex\n";
            return 1;
        }
    }

    if (!addition::sig_scheme_allowed_strict(addition::SigScheme::SlhDsaShake256s)) {
        try {
            (void)addition::generate_wallet_keys("slh-dsa-shake-256s");
            std::cerr << "test failed: SLH createwallet must fail closed when ctx-sign is unavailable\n";
            return 1;
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            if (msg.find("unknown scheme rejected in strict mode") == std::string::npos) {
                std::cerr << "test failed: SLH fail-closed message: " << msg << '\n';
                return 1;
            }
        }
        if (addition::sig_scheme_available(addition::SigScheme::SlhDsaShake256s)) {
            std::cout << "SLH-DSA-SHAKE-256s present in liboqs but "
                         "OQS_SIG_sign_with_ctx_str failed; fail-closed, no fake verify\n";
        } else {
            std::cout << "SLH-DSA-SHAKE-256s unavailable in this liboqs; scheme_id hook fail-closed ok\n";
        }
        std::cout << "allowed_sig_algs=" << algs << '\n';
        return 0;
    }
    if (algs.find("slh-dsa-shake-256s") == std::string::npos) {
        std::cerr << "test failed: SLH allowed but not listed: " << algs << '\n';
        return 1;
    }

    addition::WalletKeys slh{};
    try {
        slh = addition::generate_wallet_keys("slh-dsa-shake-256s");
    } catch (const std::exception& e) {
        std::cerr << "test failed: SLH keygen: " << e.what() << '\n';
        return 1;
    }
    if (slh.algorithm != "slh-dsa-shake-256s" || slh.address.size() != 128) {
        std::cerr << "test failed: SLH vault metadata\n";
        return 1;
    }
    if (slh.address == ml.address) {
        std::cerr << "test failed: scheme_id must change the address hash\n";
        return 1;
    }
    std::string bind_err;
    if (!addition::address_binds_pubkey(slh.address, addition::SigScheme::SlhDsaShake256s, slh.public_key, bind_err)) {
        std::cerr << "test failed: SLH address bind: " << bind_err << '\n';
        return 1;
    }
    if (addition::address_binds_pubkey(slh.address, addition::SigScheme::MlDsa87, slh.public_key, bind_err)) {
        std::cerr << "test failed: SLH pubkey must not bind as ML-DSA\n";
        return 1;
    }

    if (!chain.credit_balance(slh.address, 40, "slh_seed", error)) {
        std::cerr << "test failed: credit slh: " << error << '\n';
        return 1;
    }
    addition::Wallet slh_wallet(slh.address, slh.public_key, slh.private_key);
    if (!slh_wallet.send(mempool, chain, "slh-recv", 6, 1, error)) {
        std::cerr << "test failed: SLH spend: " << error << '\n';
        return 1;
    }
    if (!miner.mine_next_block("miner", 200, 1, mined, error)) {
        std::cerr << "test failed: mine slh: " << error << '\n';
        return 1;
    }
    if (chain.balance_of("slh-recv") != 6) {
        std::cerr << "test failed: SLH-DSA vault spend did not confirm\n";
        return 1;
    }

    std::cout << "SLH-DSA vault and ML-DSA-87 coexistence tests passed\n";
    std::cout << "allowed_sig_algs=" << algs << '\n';
    return 0;
}
