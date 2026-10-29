#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "addition/consensus_rules.hpp"

namespace addition {

enum class NetworkMode {
    Testnet,
    Mainnet,
    Regtest,
    Fast, // ADDITION_FAST_V1 research; typed stages exist; boot fail-closed until pipeline ships
};

enum class PowAlgorithm {
    Sha3_512,
    MemoryHard,
};

// Testnet PoW knobs. Easy = ~2^16 SHA3-512 header hashes; hard can approach ~60s.
// Easy is also the ceiling: 2^56-1 (old max) mined in milliseconds.
inline constexpr std::uint64_t kTestnetEasyDifficultyTarget = 0x0000FFFFFFFFFFFFULL;
inline constexpr std::uint64_t kTestnetHardDifficultyTarget = 0x000000FFFFFFFFFFULL;

inline constexpr const char* kTestnetNetworkId = "ADDITION_TESTNET_V1";
inline constexpr const char* kMainnetNetworkId = "ADDITION_MAINNET_V1";
inline constexpr const char* kRegtestNetworkId = "ADDITION_REGTEST_V1";
inline constexpr const char* kFastNetworkId = "ADDITION_FAST_V1";
inline constexpr const char* kTestnetNetworkName = "addition-testnet";
inline constexpr const char* kMainnetNetworkName = "addition-mainnet";
inline constexpr const char* kRegtestNetworkName = "addition-regtest";
inline constexpr const char* kFastNetworkName = "addition-fast";
inline constexpr std::uint64_t kTestnetGenesisTimestamp = 1'763'000'000ULL;
inline constexpr std::uint64_t kMainnetGenesisTimestamp = 1'770'000'000ULL;
inline constexpr std::uint64_t kRegtestGenesisTimestamp = 1'765'000'000ULL;
inline constexpr std::uint64_t kFastGenesisTimestamp = 1'780'000'000ULL;
// Local --regtest only. Higher target = easier. This is min-diff (any SHA3-512
// header hash meets it). Do not use on ADDITION_MAINNET_V1.
inline constexpr std::uint64_t kRegtestMinDifficultyTarget = 0xFFFFFFFFFFFFFFFFULL;
// Mainnet stays at the existing testnet *hard* floor and cannot retarget to the
// ~4ms easy target. PoW is memory_hard (1 MiB x 16 rounds per attempt).
inline constexpr std::uint64_t kMainnetDifficultyTarget = kTestnetHardDifficultyTarget;
inline constexpr std::uint64_t kMainnetMaxDifficultyTarget = kTestnetHardDifficultyTarget;
// Mainnet hardest allowed target (consensus safety bound for LWMA). 2^16x harder
// than the floor; the easiest bound stays kMainnetMaxDifficultyTarget so mainnet
// can never be loosened below the testnet hard target.
inline constexpr std::uint64_t kMainnetMinDifficultyTarget = 0x0000000000FFFFFFULL;
// Height at which the LWMA per-block retarget activates. 0 = from genesis
// (requires a mainnet relaunch). For an in-place fork set to tip+margin.
inline constexpr std::uint64_t kMainnetDifficultyForkHeight = 0ULL;
// Sentinel: never activate LWMA (legacy window retarget forever).
// Mainnet minimum tx fee (base units). Consensus: txs below are rejected in blocks.
inline constexpr std::uint64_t kMainnetMinFee = 1ULL;
// Hard-capped emission (no tail), 20 s blocks. 4 coins/block, halving every
// 5,681,819 blocks (~3.6 years) with a floor of 1 coin/block: 4 -> 2 -> 1,
// then 1/block until the cap. Supply lands on exactly 50,000,000 at height
// 27,272,727 (~17.3 years); ~99% by ~17.0 years. After that subsidy = 0 and
// miners/stakers earn fees only. (The floor is clipped by max_supply, so it is
// not a tail emission.)
inline constexpr std::uint32_t kMainnetTargetBlockTimeSec = 20U;
inline constexpr std::uint64_t kMainnetMaxSupply = 50'000'000ULL;
inline constexpr std::uint64_t kMainnetBlockReward = 4ULL;
inline constexpr std::uint32_t kMainnetHalvingInterval = 5'681'819U;
inline constexpr std::uint64_t kMainnetSubsidyFloor = 1ULL;
inline constexpr std::uint64_t kMainnetCapHeight = 27'272'727ULL;
// LWMA window kept at ~1 hour of blocks (180 x 20 s).
inline constexpr std::uint32_t kMainnetLwmaWindow = 180U;
// Staking windows rescaled x3 in blocks to keep wall-clock time:
// maturity 768 blocks (~4.3 h), unbonding 4320 blocks (~1 day).
inline constexpr std::uint64_t kMainnetStakeMaturity = 768ULL;
inline constexpr std::uint64_t kMainnetStakeUnbondDelay = 4320ULL;
// Hybrid PoW/PoS defaults (see staking.hpp). Mainnet activation is a
// launch decision; Never until vote gossip ships in the P2P layer.
inline constexpr std::uint64_t kStakingNever = UINT64_MAX;
inline constexpr std::uint64_t kDifficultyForkNever = 0xFFFFFFFFFFFFFFFFULL;

// Testnet mine search deadline. Easy SHA3-512 should finish well inside this.
inline constexpr std::uint64_t kTestnetMineDeadlineSec = 30;
// Mainnet memory_hard at 0x000000FFFFFFFFFF is ~2^24 hashes. The 30s testnet
// leftover must not abort that search. 0 = run until a block is found.
inline constexpr std::uint64_t kMainnetMineDeadlineSec = 0;

struct ChainConfig {
    std::string network_mode{"testnet"};
    std::string network_name{"addition-testnet"};
    std::string network_id{"ADDITION_TESTNET_V1"};
    std::uint64_t genesis_timestamp{1'763'000'000ULL};
    std::uint64_t max_supply{50'000'000ULL};
    std::uint64_t block_reward{50ULL};
    std::uint32_t target_block_time_sec{60U};
    std::uint32_t difficulty_window{120U};
    PowAlgorithm pow_algorithm{PowAlgorithm::Sha3_512};
    // SHA3-512 first 64 bits vs target. Higher target = easier.
    // Easy bound (~2^16 hashes) is the ceiling: 2^56-1 mined in milliseconds.
    std::uint64_t initial_difficulty_target{kTestnetEasyDifficultyTarget};
    std::uint64_t min_difficulty_target{kTestnetHardDifficultyTarget};
    std::uint64_t max_difficulty_target{kTestnetEasyDifficultyTarget};
    std::string pow_profile{"shared-testnet"};
    std::uint32_t confirmations_policy{2};
    std::string economic_security{"none"};
    std::uint32_t retarget_window{30U};
    // LWMA per-block retarget (consensus). Blocks with height >= fork use LWMA.
    std::uint64_t difficulty_fork_height{kDifficultyForkNever};
    std::uint32_t lwma_window{60U};
    // Hardest bound applied by the *legacy* rule before the fork (0 = min_difficulty_target).
    // Lets mainnet replay its frozen pre-fork history after min was widened.
    std::uint64_t legacy_min_difficulty_target{0ULL};
    std::uint32_t halving_interval{210000U};
    std::uint64_t subsidy_floor{0ULL}; // min halved reward until the cap (mainnet 1)
    bool require_pq_signatures{true};
    bool require_privacy_pool{true};
    bool allow_zero_reward_blocks{true};
    std::uint64_t min_fee{0ULL};
    // Hybrid PoW/PoS (Decred-style tickets). Consensus when height >= activation.
    std::uint64_t staking_activation_height{UINT64_MAX};
    std::uint64_t stake_min_amount{1000ULL};       // min coins per ticket
    std::uint64_t stake_maturity{256ULL};          // blocks before a ticket can be selected
    std::uint64_t stake_unbond_delay{1440ULL};     // blocks unlocked coins stay frozen
    std::uint32_t stake_votes_per_block{5U};       // tickets selected per block
    std::uint32_t stake_vote_quorum{3U};           // approving votes required
    std::uint64_t stake_min_live_tickets{5ULL};    // below this pool size, votes are optional
    std::uint32_t stake_reward_bps{3000U};         // stakers' share of subsidy+fees (30%)
    std::vector<std::string> bootstrap_peers{"127.0.0.1:28545"};
};

struct NodeConfig {
    NetworkMode mode{NetworkMode::Testnet};
    ChainConfig chain{};
    std::uint16_t local_rpc_port{8545};
    std::uint16_t lan_rpc_port{18545};
    std::uint16_t p2p_port{28545};
    std::uint16_t public_rpc_port{38545};
    std::string public_rpc_bind{"0.0.0.0"};
    bool enable_public_rpc{false};
    bool enable_auto_mine{false};
    std::uint32_t auto_mine_interval_sec{60};
    std::string auto_mine_reward{"miner1"};
    std::vector<std::string> bootstrap_peers{"127.0.0.1:28545"};
    // Public advertisement only. Not added to the internal peer set (avoids self-sync).
    std::string advertised_p2p{};
    std::string data_dir{"data"};
    std::string config_path{};
    std::string genesis_path{};
};

// Operator's current public P2P (IPv4 only). Not a peer-count claim.
// Seed operators set ADDITION_ADVERTISED_P2P to this so public getinfo/peers
// do not list `self`. Public TCP 28545 can timeout or be filtered; HTTP :80
// sync is the reliable join path for testnet.
inline constexpr const char* kOperatorPublicP2p = "34.27.30.115:28545";
// Public mainnet P2P seed (ADDITION_MAINNET_V1). Distinct from the testnet seed.
// Home miners: --mainnet --bootstrap 34.27.30.115:28546. Write RPC stays loopback.
inline constexpr const char* kOperatorPublicMainnetP2p = "34.27.30.115:28546";

bool is_ipv4_endpoint(const std::string& endpoint);
bool is_loopback_host(const std::string& host);
bool is_loopback_endpoint(const std::string& endpoint);
bool is_self_peer_label(const std::string& endpoint);
bool is_external_advertised_peer(const std::string& endpoint);
bool parse_advertised_p2p(const std::string& value, std::string& out, std::string& error);
void apply_advertised_p2p_env(NodeConfig& cfg);
std::vector<std::string> public_advertised_peers(const std::vector<std::string>& peers,
                                                 const std::string& advertised_p2p);

const ChainConfig& default_config();
ChainConfig testnet_chain_config();
ChainConfig mainnet_chain_config();
ChainConfig regtest_chain_config();
ChainConfig fast_chain_config();
NodeConfig default_node_config();
NodeConfig mainnet_node_config();
NodeConfig regtest_node_config();
NodeConfig fast_node_config();
void apply_regtest_profile(ChainConfig& chain);
void apply_fast_profile(ChainConfig& chain);
bool validate_network_profile(const NodeConfig& cfg, std::string& error);

void set_runtime_network_mode(NetworkMode mode);
NetworkMode runtime_network_mode();
bool is_mainnet_runtime();
const char* network_mode_label(NetworkMode mode);
NetworkMode parse_network_mode(const std::string& value, bool& ok);
const char* pow_algorithm_label(PowAlgorithm algorithm);
PowAlgorithm parse_pow_algorithm(const std::string& value, bool& ok);
std::uint64_t mine_deadline_seconds(const ChainConfig& cfg);
std::size_t default_mine_thread_count();

bool load_toml_config(const std::string& path, NodeConfig& cfg, std::string& error);
bool load_genesis_json(const std::string& path, ChainConfig& chain, std::string& error);
bool apply_cli_args(int argc, char** argv, NodeConfig& cfg, bool& show_help, std::string& error);
std::string daemon_help_text();

} // namespace addition
