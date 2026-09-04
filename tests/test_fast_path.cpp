#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/fast_path.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/privacy.hpp"
#include "addition/rpc_server.hpp"
#include "addition/staking.hpp"
#include "addition/contract_engine.hpp"
#include "addition/bridge.hpp"
#include "addition/token_engine.hpp"
#include "addition/p2p.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/private_messaging.hpp"
#include "addition/ai_optimizer.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/wallet_keys.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {

bool expect_contains(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test failed: missing " << label << " (" << needle << ") in: " << hay << '\n';
        return false;
    }
    return true;
}

bool expect_absent(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) != std::string::npos) {
        std::cerr << "test failed: must not contain " << label << " (" << needle << ") in: " << hay << '\n';
        return false;
    }
    return true;
}

bool load_checked_genesis(const char* path, addition::ChainConfig& chain, std::string& err) {
    return addition::load_genesis_json(path, chain, err) ||
           addition::load_genesis_json((std::string("../") + path).c_str(), chain, err);
}

} // namespace

int main() {
    // --- Mainnet PoW path unchanged ---
    const auto mainnet = addition::mainnet_chain_config();
    if (mainnet.network_id != addition::kMainnetNetworkId ||
        mainnet.pow_algorithm != addition::PowAlgorithm::MemoryHard ||
        mainnet.initial_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet.min_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet.max_difficulty_target != 0x000000FFFFFFFFFFULL) {
        std::cerr << "test failed: mainnet memory_hard target must stay 0x000000FFFFFFFFFF\n";
        return 1;
    }
    if (addition::consensus_path_label(mainnet) != std::string("memory_hard_pow") ||
        addition::fast_path_status_label(mainnet) != std::string("not_this_network")) {
        std::cerr << "test failed: mainnet consensus/fast_path labels\n";
        return 1;
    }

    addition::NodeConfig mainnet_node = addition::mainnet_node_config();
    std::string err;
    if (!addition::validate_network_profile(mainnet_node, err)) {
        std::cerr << "test failed: mainnet profile must validate: " << err << '\n';
        return 1;
    }

    // --- Fast profile scaffolding ---
    const auto fast = addition::fast_chain_config();
    if (fast.network_id != addition::kFastNetworkId || fast.network_mode != "fast" ||
        fast.network_name != addition::kFastNetworkName ||
        fast.genesis_timestamp != addition::kFastGenesisTimestamp) {
        std::cerr << "test failed: fast chain ids\n";
        return 1;
    }
    if (addition::consensus_path_label(fast) != std::string("leader_pipeline_scaffold") ||
        addition::fast_path_status_label(fast) != std::string("scaffold_incomplete") ||
        addition::throughput_claim_label() != std::string("none") ||
        addition::fast_path_pipeline_shipped()) {
        std::cerr << "test failed: fast path honesty labels\n";
        return 1;
    }
    {
        std::string boot_err;
        if (addition::fast_path_boot_allowed(boot_err)) {
            std::cerr << "test failed: fast path must fail-closed while incomplete\n";
            return 1;
        }
        if (boot_err.find("fail-closed") == std::string::npos ||
            boot_err.find("ADDITION_FAST_V1") == std::string::npos) {
            std::cerr << "test failed: boot error must mention fail-closed FAST_V1: " << boot_err << '\n';
            return 1;
        }
    }

    addition::NodeConfig fast_node = addition::fast_node_config();
    if (addition::validate_network_profile(fast_node, err)) {
        std::cerr << "test failed: incomplete fast profile must not validate\n";
        return 1;
    }
    if (err.find("fail-closed") == std::string::npos && err.find("scaffold incomplete") == std::string::npos) {
        std::cerr << "test failed: fast validate error: " << err << '\n';
        return 1;
    }

    // CLI --fast / --network fast refuse to apply (fail-closed).
    {
        char arg0[] = "additiond";
        char arg1[] = "--fast";
        char* argv[] = {arg0, arg1};
        addition::NodeConfig cfg;
        bool help = false;
        std::string cli_err;
        if (addition::apply_cli_args(2, argv, cfg, help, cli_err)) {
            std::cerr << "test failed: --fast must fail-closed while scaffold incomplete\n";
            return 1;
        }
        if (cli_err.find("ADDITION_FAST_V1") == std::string::npos) {
            std::cerr << "test failed: --fast error: " << cli_err << '\n';
            return 1;
        }
    }
    {
        char arg0[] = "additiond";
        char arg1[] = "--network";
        char arg2[] = "fast";
        char* argv[] = {arg0, arg1, arg2};
        addition::NodeConfig cfg;
        bool help = false;
        std::string cli_err;
        if (addition::apply_cli_args(3, argv, cfg, help, cli_err)) {
            std::cerr << "test failed: --network fast must fail-closed\n";
            return 1;
        }
    }

    // --mainnet still works and keeps memory_hard.
    {
        char arg0[] = "additiond";
        char arg1[] = "--mainnet";
        char* argv[] = {arg0, arg1};
        addition::NodeConfig cfg;
        bool help = false;
        std::string cli_err;
        if (!addition::apply_cli_args(2, argv, cfg, help, cli_err)) {
            std::cerr << "test failed: --mainnet must still apply: " << cli_err << '\n';
            return 1;
        }
        if (cfg.mode != addition::NetworkMode::Mainnet ||
            cfg.chain.network_id != addition::kMainnetNetworkId ||
            cfg.chain.pow_algorithm != addition::PowAlgorithm::MemoryHard ||
            cfg.chain.initial_difficulty_target != 0x000000FFFFFFFFFFULL) {
            std::cerr << "test failed: --mainnet PoW path changed\n";
            return 1;
        }
    }

    // genesis-fast.json stub loads with correct id (construction only; boot still fail-closed).
    {
        addition::ChainConfig file = addition::fast_chain_config();
        std::string gerr;
        if (!load_checked_genesis("genesis-fast.json", file, gerr)) {
            std::cerr << "test failed: load genesis-fast.json: " << gerr << '\n';
            return 1;
        }
        if (file.network_id != addition::kFastNetworkId || file.network_mode != "fast") {
            std::cerr << "test failed: genesis-fast.json network id\n";
            return 1;
        }
    }

    // getinfo on mainnet-like RPC distinguishes PoW vs fast roadmap without Solana TPS lies.
    {
        addition::Chain chain(mainnet);
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
        addition::WalletKeys keys{};
        try {
            keys = addition::generate_wallet_keys();
        } catch (const std::exception& e) {
            std::cerr << "test failed: generate_wallet_keys: " << e.what() << '\n';
            return 1;
        }
        addition::DecentralizedNode node("self",
                                         keys.public_key,
                                         keys.private_key,
                                         chain,
                                         mempool,
                                         peers,
                                         consensus);
        addition::RpcServer rpc(chain, mempool, miner, staking, contracts, bridge, tokens, peers,
                                consensus, privacy, pouw_storage, pouw_compute, private_messaging,
                                ai_optimizer, node, false, true);

        const auto info = rpc.handle_command("getinfo");
        if (!expect_contains(info, "network_id=ADDITION_MAINNET_V1", "mainnet id") ||
            !expect_contains(info, "pow_algorithm=memory_hard", "mainnet pow") ||
            !expect_contains(info, "consensus_path=memory_hard_pow", "consensus path") ||
            !expect_contains(info, "fast_path_status=not_this_network", "fast status") ||
            !expect_contains(info, "fast_path_network_id=ADDITION_FAST_V1", "fast id") ||
            !expect_contains(info, "fast_path_shipped=false", "fast shipped") ||
            !expect_contains(info, "fast_path_slice=pipeline_stages_typed_v1", "fast slice") ||
            !expect_contains(info, "throughput_claim=none", "throughput claim") ||
            !expect_contains(info, "research_goal_is_not_a_measurement=true", "goal label") ||
            !expect_contains(info, "pq_mode=strict", "pq mode") ||
            !expect_contains(info, "privacy_claim=opening_not_zk", "privacy claim")) {
            return 1;
        }
        if (!expect_absent(info, "solana_tps", "solana tps") ||
            !expect_absent(info, "measured_solana", "measured solana") ||
            !expect_absent(info, "throughput_claim=solana", "solana claim") ||
            !expect_absent(info, "objective_tps_ok", "objective ok")) {
            return 1;
        }

        const auto pstat = rpc.handle_command("protocol_status");
        if (!expect_contains(pstat, "research_goal_is_not_a_measurement=true", "protocol goal") ||
            !expect_contains(pstat, "consensus_path=memory_hard_pow", "protocol consensus") ||
            !expect_contains(pstat, "throughput_claim=none", "protocol throughput")) {
            return 1;
        }
        if (!expect_absent(pstat, "objective_tps_ok", "protocol objective ok")) {
            return 1;
        }
    }

    // Fast info fields never claim measured Solana TPS.
    {
        const auto fields = addition::fast_path_info_fields(fast);
        if (!expect_contains(fields, "consensus_path=leader_pipeline_scaffold", "fast consensus") ||
            !expect_contains(fields, "fast_path_status=scaffold_incomplete", "fast status") ||
            !expect_contains(fields, "fast_path_slice=pipeline_stages_typed_v1", "fast slice") ||
            !expect_contains(fields, "throughput_claim=none", "fast throughput")) {
            return 1;
        }
        if (!expect_absent(fields, "solana", "solana in fast fields") ||
            !expect_absent(fields, "measured_tps=65000", "fake solana tps") ||
            !expect_absent(fields, "tps=100000", "fake 100k as measurement")) {
            return 1;
        }
    }

    // --- Pipeline stages + typed messages (REAL validation; not consensus) ---
    {
        const auto order = addition::fast_pipeline_stage_order();
        if (order.size() != 6 || order.front() != addition::FastPipelineStage::Idle ||
            order.back() != addition::FastPipelineStage::Committed) {
            std::cerr << "test failed: stage order\n";
            return 1;
        }
        if (std::string(addition::fast_path_slice_label()) != "pipeline_stages_typed_v1") {
            std::cerr << "test failed: slice label\n";
            return 1;
        }

        addition::FastPipelineBatch batch(42);
        if (batch.stage() != addition::FastPipelineStage::Idle) {
            std::cerr << "test failed: batch starts Idle\n";
            return 1;
        }

        const addition::FastMessageKind kinds[] = {
            addition::FastMessageKind::IngestBatch,
            addition::FastMessageKind::ScheduleTicket,
            addition::FastMessageKind::ExecutionReceipt,
            addition::FastMessageKind::VerifyAck,
            addition::FastMessageKind::CommitSeal,
        };
        const addition::FastPipelineStage after[] = {
            addition::FastPipelineStage::Ingested,
            addition::FastPipelineStage::Scheduled,
            addition::FastPipelineStage::Executed,
            addition::FastPipelineStage::Verified,
            addition::FastPipelineStage::Committed,
        };
        for (std::size_t i = 0; i < 5; ++i) {
            addition::FastPipelineMessage msg;
            msg.kind = kinds[i];
            msg.batch_id = 42;
            msg.network_id = addition::kFastNetworkId;
            msg.body = std::string("research-batch-step-") + addition::fast_message_kind_label(kinds[i]);
            addition::fast_pipeline_seal_digest(msg);
            std::string apply_err;
            if (!batch.apply(msg, apply_err)) {
                std::cerr << "test failed: apply step " << i << ": " << apply_err << '\n';
                return 1;
            }
            if (batch.stage() != after[i]) {
                std::cerr << "test failed: stage after step " << i << '\n';
                return 1;
            }
        }
        {
            std::string done_err;
            addition::FastMessageKind next{};
            if (addition::fast_pipeline_next_kind(batch.stage(), next, done_err)) {
                std::cerr << "test failed: committed batch must refuse next kind\n";
                return 1;
            }
        }
    }

    // Fail-closed: wrong network, bad digest, out-of-order kind, fake TPS magic.
    {
        addition::FastPipelineBatch batch(7);
        addition::FastPipelineMessage msg;
        msg.kind = addition::FastMessageKind::IngestBatch;
        msg.batch_id = 7;
        msg.network_id = addition::kMainnetNetworkId;
        msg.body = "x";
        addition::fast_pipeline_seal_digest(msg);
        std::string err;
        if (batch.apply(msg, err) || err.find("ADDITION_FAST_V1") == std::string::npos) {
            std::cerr << "test failed: mainnet network_id must be rejected: " << err << '\n';
            return 1;
        }

        msg.network_id = addition::kFastNetworkId;
        msg.body = "ok-body";
        addition::fast_pipeline_seal_digest(msg);
        msg.payload_digest_hex = std::string(128, '0');
        if (batch.apply(msg, err) || err.find("digest") == std::string::npos) {
            std::cerr << "test failed: bad digest must be rejected: " << err << '\n';
            return 1;
        }

        addition::fast_pipeline_seal_digest(msg);
        msg.kind = addition::FastMessageKind::CommitSeal; // skip ahead
        addition::fast_pipeline_seal_digest(msg);
        if (batch.apply(msg, err) || err.find("expected kind") == std::string::npos) {
            std::cerr << "test failed: out-of-order kind must be rejected: " << err << '\n';
            return 1;
        }

        msg.kind = addition::FastMessageKind::IngestBatch;
        msg.body = std::string("prefix ") + addition::kFastFakeTpsMagic + " suffix";
        addition::fast_pipeline_seal_digest(msg);
        if (batch.apply(msg, err) || err.find("TPS claim") == std::string::npos) {
            std::cerr << "test failed: CLAIM_MEASURED_TPS magic must be rejected: " << err << '\n';
            return 1;
        }

        msg.body = std::string("live ") + addition::kFastFakeLiveMagic;
        addition::fast_pipeline_seal_digest(msg);
        if (batch.apply(msg, err) || err.find("fail-closed") == std::string::npos) {
            std::cerr << "test failed: CLAIM_FAST_LIVE magic must be rejected: " << err << '\n';
            return 1;
        }

        msg.body = "solana_tps=65000";
        addition::fast_pipeline_seal_digest(msg);
        if (batch.apply(msg, err)) {
            std::cerr << "test failed: solana_tps body must be rejected\n";
            return 1;
        }
    }

    // SHA3-512 digest is deterministic for the domain-separated preimage.
    {
        addition::FastPipelineMessage a;
        a.kind = addition::FastMessageKind::ScheduleTicket;
        a.batch_id = 99;
        a.network_id = addition::kFastNetworkId;
        a.body = "deterministic-body";
        addition::fast_pipeline_seal_digest(a);
        addition::FastPipelineMessage b = a;
        addition::fast_pipeline_seal_digest(b);
        if (a.payload_digest_hex != b.payload_digest_hex || a.payload_digest_hex.size() != 128) {
            std::cerr << "test failed: digest not deterministic SHA3-512\n";
            return 1;
        }
        const auto pre = addition::fast_pipeline_digest_preimage(a);
        if (pre.find("addition.fast_path_v1|schedule_ticket|99|ADDITION_FAST_V1|deterministic-body") ==
            std::string::npos) {
            std::cerr << "test failed: digest preimage domain: " << pre << '\n';
            return 1;
        }
        // Full pipeline still not shipped after typed-stage progress.
        if (addition::fast_path_pipeline_shipped()) {
            std::cerr << "test failed: typed stages must not set pipeline shipped\n";
            return 1;
        }
        std::string boot_err;
        if (addition::fast_path_boot_allowed(boot_err)) {
            std::cerr << "test failed: boot must stay fail-closed after typed stages\n";
            return 1;
        }
    }

    // Distinct genesis hash from mainnet.
    {
        addition::Chain mainnet_chain(mainnet);
        addition::Chain fast_chain(fast);
        const auto mh = addition::hash_block_header(mainnet_chain.genesis_block().header);
        const auto fh = addition::hash_block_header(fast_chain.genesis_block().header);
        if (mh == fh) {
            std::cerr << "test failed: fast genesis hash must differ from mainnet\n";
            return 1;
        }
        if (fast_chain.genesis_block().header.previous_hash !=
            std::string("genesis:") + addition::kFastNetworkId) {
            std::cerr << "test failed: fast genesis must bind previous_hash to network_id\n";
            return 1;
        }
    }

    std::cout << "ok: fast path stages typed (mainnet PoW untouched; fail-closed; no fake Solana TPS)\n";
    return 0;
}
