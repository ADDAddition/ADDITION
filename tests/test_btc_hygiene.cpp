#include "addition/btc_hygiene.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/rpc_server.hpp"
#include "addition/wallet_keys.hpp"

#include "addition/ai_optimizer.hpp"
#include "addition/bridge.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/contract_engine.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/p2p.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/privacy.hpp"
#include "addition/private_messaging.hpp"
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<addition::BtcScriptSample> samples;
    std::string error;
    const char* paths[] = {"fixtures/btc_hygiene_samples.json", "../fixtures/btc_hygiene_samples.json"};
    bool loaded = false;
    std::string used_path;
    for (const char* p : paths) {
        if (addition::load_btc_hygiene_fixtures(p, samples, error)) {
            loaded = true;
            used_path = p;
            break;
        }
    }
    if (!loaded) {
        std::cerr << "test failed: load fixtures: " << error << '\n';
        return 1;
    }

    const auto reports = addition::classify_btc_samples(samples);
    if (reports.size() < 6) {
        std::cerr << "test failed: expected several fixture reports\n";
        return 1;
    }
    bool saw_reuse = false;
    bool saw_p2pk = false;
    bool saw_p2wpkh = false;
    for (const auto& r : reports) {
        if (r.address_reuse) {
            saw_reuse = true;
        }
        if (r.script_class == addition::BtcScriptClass::P2pk && r.pubkey_already_on_chain) {
            saw_p2pk = true;
        }
        if (r.script_class == addition::BtcScriptClass::P2wpkh) {
            saw_p2wpkh = true;
        }
    }
    if (!saw_reuse || !saw_p2pk || !saw_p2wpkh) {
        std::cerr << "test failed: classifier missed reuse/p2pk/p2wpkh\n";
        return 1;
    }

    addition::ChainConfig easy = addition::regtest_chain_config();
    addition::Chain chain(easy);
    addition::Mempool mempool;
    addition::Miner miner(chain, mempool);
    addition::StakingEngine staking;
    addition::TokenEngine tokens;
    addition::PrivacyPool privacy;
    addition::ContractEngine contracts(&tokens, &privacy);
    addition::BridgeEngine bridge;
    addition::PeerNetwork peers;
    addition::ConsensusEngine consensus;
    addition::PoUWStorageEngine pouw_storage;
    addition::PoUWComputeEngine pouw_compute;
    addition::PrivateMessagingEngine private_messaging;
    addition::AIRoutingOptimizer ai_optimizer;
    addition::WalletKeys keys = addition::generate_wallet_keys();
    addition::DecentralizedNode node("self", keys.public_key, keys.private_key, chain, mempool, peers, consensus);
    const auto rpc_dir = std::filesystem::temp_directory_path() / "addition-hygiene-rpc";
    std::filesystem::remove_all(rpc_dir);
    std::filesystem::create_directories(rpc_dir);
    addition::RpcServer rpc(chain, mempool, miner, staking, contracts, bridge, tokens, peers, consensus, privacy,
                            pouw_storage, pouw_compute, private_messaging, ai_optimizer, node, false, true,
                            (rpc_dir / "wallets").string());

    const auto classified = rpc.handle_command("hygiene_classify " + used_path);
    if (classified.find("ok:hygiene_rehearsal") == std::string::npos ||
        classified.find("moves_bitcoin=0") == std::string::npos ||
        classified.find("claim=attestation_not_bip360") == std::string::npos) {
        std::cerr << "test failed: hygiene_classify: " << classified << '\n';
        return 1;
    }

    const auto created = rpc.handle_command("createwallet hy");
    if (created.find("address=") == std::string::npos) {
        std::cerr << "test failed: createwallet: " << created << '\n';
        return 1;
    }
    const auto addr_pos = created.find("address=");
    const auto addr_end = created.find(' ', addr_pos);
    const auto addr = created.substr(addr_pos + 8, addr_end - addr_pos - 8);
    if (!chain.credit_balance(addr, 50, "hygiene_seed", error)) {
        std::cerr << "test failed: credit: " << error << '\n';
        return 1;
    }

    const auto attested = rpc.handle_command("hygiene_attest hy 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0");
    if (attested.find("ok:hygiene_receipt") == std::string::npos ||
        attested.find("moves_bitcoin=0") == std::string::npos ||
        attested.find("note=ADDITION-HYGIENE-REHEARSAL") == std::string::npos) {
        std::cerr << "test failed: hygiene_attest: " << attested << '\n';
        return 1;
    }
    const auto note_pos = attested.find(" note=");
    if (note_pos == std::string::npos) {
        std::cerr << "test failed: hygiene_attest missing trailing note=: " << attested << '\n';
        return 1;
    }
    const auto note = attested.substr(note_pos + 6);
    const auto verified = rpc.handle_command("hygiene_verify " + note);
    if (verified.find("ok:hygiene_receipt") == std::string::npos) {
        std::cerr << "test failed: hygiene_verify good: " << verified << '\n';
        return 1;
    }

    auto garbage = note;
    const auto class_pos = garbage.find("class=p2pkh");
    if (class_pos != std::string::npos) {
        garbage.replace(class_pos, 11, "class=p2trX");
    }
    const auto rejected = rpc.handle_command("hygiene_verify " + garbage);
    if (rejected.find("garbage hygiene receipt rejected") == std::string::npos &&
        rejected.find("error:") != 0) {
        std::cerr << "test failed: garbage receipt must be rejected: " << rejected << '\n';
        return 1;
    }

    std::filesystem::remove_all(rpc_dir);
    std::cout << "btc hygiene tests passed\n";
    return 0;
}
