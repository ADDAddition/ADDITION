#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace addition {

enum class NetworkMode {
    Testnet,
    Mainnet,
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
inline constexpr const char* kTestnetNetworkName = "addition-testnet";
inline constexpr const char* kMainnetNetworkName = "addition-mainnet";
inline constexpr std::uint64_t kTestnetGenesisTimestamp = 1'763'000'000ULL;
inline constexpr std::uint64_t kMainnetGenesisTimestamp = 1'770'000'000ULL;
// Mainnet stays at the existing testnet *hard* floor and cannot retarget to the
// ~4ms easy target. PoW is memory_hard (1 MiB x 16 rounds per attempt).
inline constexpr std::uint64_t kMainnetDifficultyTarget = kTestnetHardDifficultyTarget;
inline constexpr std::uint64_t kMainnetMaxDifficultyTarget = kTestnetHardDifficultyTarget;

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
    std::uint64_t tail_emission_reward{1ULL};
    std::uint32_t target_block_time_sec{60U};
    std::uint32_t difficulty_window{120U};
    PowAlgorithm pow_algorithm{PowAlgorithm::Sha3_512};
    // SHA3-512 first 64 bits vs target. Higher target = easier.
    // Easy bound (~2^16 hashes) is the ceiling: 2^56-1 mined in milliseconds.
    std::uint64_t initial_difficulty_target{kTestnetEasyDifficultyTarget};
    std::uint64_t min_difficulty_target{kTestnetHardDifficultyTarget};
    std::uint64_t max_difficulty_target{kTestnetEasyDifficultyTarget};
    std::uint32_t retarget_window{30U};
    std::uint32_t halving_interval{210000U};
    bool require_pq_signatures{true};
    bool require_privacy_pool{true};
    bool allow_zero_reward_blocks{true};
    std::uint64_t min_fee{1ULL};
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
// sync is the reliable join path.
inline constexpr const char* kOperatorPublicP2p = "34.27.30.115:28545";

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
NodeConfig default_node_config();
NodeConfig mainnet_node_config();
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
