#include "addition/ai_optimizer.hpp"
#include "addition/bridge.hpp"
#include "addition/btc_hygiene.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/contract_engine.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/p2p.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/privacy.hpp"
#include "addition/private_messaging.hpp"
#include "addition/rpc_access.hpp"
#include "addition/rpc_server.hpp"
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"
#include "addition/wallet_keys.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect_contains(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test failed: " << label << " missing [" << needle << "] in [" << hay << "]\n";
        return false;
    }
    return true;
}

} // namespace

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
    bool saw_p2tr = false;
    bool saw_p2sh = false;
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
        if (r.script_class == addition::BtcScriptClass::P2tr) {
            saw_p2tr = true;
        }
        if (r.script_class == addition::BtcScriptClass::P2sh) {
            saw_p2sh = true;
        }
    }
    if (!saw_reuse || !saw_p2pk || !saw_p2wpkh || !saw_p2tr || !saw_p2sh) {
        std::cerr << "test failed: classifier missed reuse/p2pk/p2wpkh/p2tr/p2sh\n";
        return 1;
    }

    addition::BtcHygieneReport sample{};
    sample.address = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
    sample.height = 170;
    sample.class_name = "p2pkh";
    sample.script_class = addition::BtcScriptClass::P2pkh;
    const auto body = addition::hygiene_receipt_body(sample);
    if (body.find("ADDITION-HYGIENE-REHEARSAL|v1") != 0 ||
        body.find("moves_bitcoin=0") == std::string::npos ||
        body.find("claim=attestation_not_bip360") == std::string::npos) {
        std::cerr << "test failed: receipt body label: " << body << '\n';
        return 1;
    }

    addition::BtcHygieneReport parsed{};
    if (!addition::parse_hygiene_receipt_body(body, parsed, error) ||
        parsed.address != sample.address || parsed.class_name != "p2pkh") {
        std::cerr << "test failed: parse good body: " << error << '\n';
        return 1;
    }

    auto moves_claim = body;
    const auto moves_pos = moves_claim.find("moves_bitcoin=0");
    if (moves_pos == std::string::npos) {
        std::cerr << "test failed: body missing moves_bitcoin=0\n";
        return 1;
    }
    moves_claim.replace(moves_pos, 15, "moves_bitcoin=1");
    if (addition::parse_hygiene_receipt_body(moves_claim, parsed, error) ||
        error.find("moves_bitcoin=0") == std::string::npos) {
        std::cerr << "test failed: moves_bitcoin=1 must be rejected: " << error << '\n';
        return 1;
    }

    auto bip_claim = body;
    const auto claim_pos = bip_claim.find("claim=attestation_not_bip360");
    if (claim_pos == std::string::npos) {
        std::cerr << "test failed: body missing claim\n";
        return 1;
    }
    bip_claim.replace(claim_pos, 27, "claim=bip360_live_fork");
    if (addition::parse_hygiene_receipt_body(bip_claim, parsed, error) ||
        error.find("attestation_not_bip360") == std::string::npos) {
        std::cerr << "test failed: BIP-360 claim must be rejected: " << error << '\n';
        return 1;
    }

    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
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
    if (!expect_contains(classified, "ok:hygiene_rehearsal", "classify prefix") ||
        !expect_contains(classified, "moves_bitcoin=0", "classify moves") ||
        !expect_contains(classified, "claim=attestation_not_bip360", "classify claim")) {
        return 1;
    }

    const auto created = rpc.handle_command("createwallet hy");
    if (created.find("address=") == std::string::npos) {
        std::cerr << "test failed: createwallet: " << created << '\n';
        return 1;
    }

    const auto attested = rpc.handle_command("hygiene_attest hy 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0");
    if (!expect_contains(attested, "ok:hygiene_receipt", "attest prefix") ||
        !expect_contains(attested, "moves_bitcoin=0", "attest moves") ||
        !expect_contains(attested, "note=ADDITION-HYGIENE-REHEARSAL", "attest note") ||
        !expect_contains(attested, "claim=attestation_not_bip360", "attest claim")) {
        return 1;
    }
    const auto note_pos = attested.find(" note=");
    if (note_pos == std::string::npos) {
        std::cerr << "test failed: hygiene_attest missing trailing note=: " << attested << '\n';
        return 1;
    }
    const auto note = attested.substr(note_pos + 6);
    const auto verified = rpc.handle_command("hygiene_verify " + note);
    if (!expect_contains(verified, "ok:hygiene_receipt", "verify good")) {
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

    if (rpc.handle_command("hygiene_attest hy 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0", false)
            .find("error:") != 0) {
        std::cerr << "test failed: untrusted hygiene_attest must be rejected\n";
        return 1;
    }
    if (addition::is_public_read_command("hygiene_classify") ||
        addition::is_public_read_command("hygiene_attest") ||
        addition::is_public_read_command("hygiene_verify") ||
        addition::is_remote_allowed_command("hygiene_attest")) {
        std::cerr << "test failed: hygiene write/classify must stay off public RPC\n";
        return 1;
    }

    std::filesystem::remove_all(rpc_dir);
    std::cout << "btc hygiene tests passed\n";
    return 0;
}
