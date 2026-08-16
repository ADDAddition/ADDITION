#include "addition/ai_optimizer.hpp"
#include "addition/bridge.hpp"
#include "addition/btc_hygiene.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/contract_engine.hpp"
#include "addition/crypto.hpp"
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

std::string load_fixture_path(std::vector<addition::BtcScriptSample>& samples, std::string& error) {
    const auto from_source = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                              "fixtures" / "btc_hygiene_samples.json")
                                 .lexically_normal()
                                 .string();
    const std::string paths[] = {
        "fixtures/btc_hygiene_samples.json",
        from_source,
    };
    for (const auto& p : paths) {
        if (addition::load_btc_hygiene_fixtures(p, samples, error)) {
            return p;
        }
    }
    return {};
}

} // namespace

int main() {
    if (addition::classify_btc_script("76a91489abcdefabbaabbaabbaabbaabbaabbaabbaabba88ac") !=
        addition::BtcScriptClass::P2pkh) {
        std::cerr << "test failed: p2pkh script class\n";
        return 1;
    }
    if (addition::classify_btc_script("0020aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") !=
        addition::BtcScriptClass::P2wsh) {
        std::cerr << "test failed: p2wsh script class\n";
        return 1;
    }
    if (addition::classify_btc_script("deadbeef") != addition::BtcScriptClass::Unknown) {
        std::cerr << "test failed: unknown script class\n";
        return 1;
    }

    std::vector<addition::BtcScriptSample> samples;
    std::string error;
    const auto used_path = load_fixture_path(samples, error);
    if (used_path.empty()) {
        std::cerr << "test failed: load fixtures: " << error << '\n';
        return 1;
    }
    if (!addition::load_btc_hygiene_fixtures("../etc/passwd", samples, error) &&
        error.find("unsafe") == std::string::npos) {
        std::cerr << "test failed: expected unsafe path reject, got: " << error << '\n';
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

    addition::BtcHygieneReport sample = reports.front();
    const auto body = addition::hygiene_receipt_body(sample);
    if (body.find("ADDITION-HYGIENE-REHEARSAL|v1|") != 0 ||
        body.find("moves_bitcoin=0") == std::string::npos ||
        body.find("claim=attestation_not_bip360") == std::string::npos) {
        std::cerr << "test failed: receipt body labels: " << body << '\n';
        return 1;
    }
    addition::BtcHygieneReport parsed{};
    if (!addition::parse_hygiene_receipt_body(body, parsed, error)) {
        std::cerr << "test failed: parse good receipt: " << error << '\n';
        return 1;
    }
    auto missing_claim = body;
    const auto claim_pos = missing_claim.find("|claim=attestation_not_bip360");
    if (claim_pos == std::string::npos) {
        std::cerr << "test failed: receipt missing claim token\n";
        return 1;
    }
    missing_claim.erase(claim_pos);
    if (addition::parse_hygiene_receipt_body(missing_claim, parsed, error)) {
        std::cerr << "test failed: receipt without claim must fail\n";
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
    if (!expect_contains(classified, "ok:hygiene_rehearsal", "classify status") ||
        !expect_contains(classified, "moves_bitcoin=0", "classify moves") ||
        !expect_contains(classified, "claim=attestation_not_bip360", "classify claim")) {
        return 1;
    }

    if (addition::is_public_read_command("hygiene_classify") ||
        addition::is_public_read_command("hygiene_attest") ||
        addition::is_public_read_command("hygiene_verify")) {
        std::cerr << "test failed: hygiene commands must stay off public RPC\n";
        return 1;
    }
    const auto public_blocked = rpc.handle_command("hygiene_classify " + used_path, false);
    if (public_blocked.find("error: command disabled") == std::string::npos) {
        std::cerr << "test failed: untrusted hygiene_classify: " << public_blocked << '\n';
        return 1;
    }

    const auto created = rpc.handle_command("createwallet hy");
    if (created.find("address=") == std::string::npos) {
        std::cerr << "test failed: createwallet: " << created << '\n';
        return 1;
    }

    const auto attested = rpc.handle_command("hygiene_attest hy 1BoatSLRHtKNngkdXEeobR76b53LETtpyT 170 p2pkh 0 0");
    if (!expect_contains(attested, "ok:hygiene_receipt", "attest status") ||
        !expect_contains(attested, "moves_bitcoin=0", "attest moves") ||
        !expect_contains(attested, "claim=attestation_not_bip360", "attest claim") ||
        !expect_contains(attested, "note=ADDITION-HYGIENE-REHEARSAL", "attest note")) {
        return 1;
    }
    const auto note_pos = attested.find(" note=");
    if (note_pos == std::string::npos) {
        std::cerr << "test failed: hygiene_attest missing trailing note=: " << attested << '\n';
        return 1;
    }
    if (attested.find(" note=", note_pos + 1) != std::string::npos) {
        std::cerr << "test failed: note= must be last so the signed body is not truncated\n";
        return 1;
    }
    const auto note = attested.substr(note_pos + 6);
    const auto verified = rpc.handle_command("hygiene_verify " + note);
    if (!expect_contains(verified, "ok:hygiene_receipt", "verify good")) {
        return 1;
    }

    auto garbage = note;
    const auto class_pos = garbage.find("class=p2pkh");
    if (class_pos == std::string::npos) {
        std::cerr << "test failed: attested note missing class=p2pkh\n";
        return 1;
    }
    garbage.replace(class_pos, 11, "class=p2trX");
    const auto rejected = rpc.handle_command("hygiene_verify " + garbage);
    if (rejected.find("garbage hygiene receipt rejected") == std::string::npos) {
        std::cerr << "test failed: garbage receipt must be rejected: " << rejected << '\n';
        return 1;
    }

    std::filesystem::remove_all(rpc_dir);
    std::cout << "btc hygiene tests passed\n";
    return 0;
}
