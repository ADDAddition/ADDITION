#pragma once

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
    std::uint64_t initial_difficulty_target{0x0000FFFFFFFFFFFFULL};
    std::uint64_t min_difficulty_target{0x0000FFFFFFFFFFFFULL};
    std::uint64_t max_difficulty_target{0x00FFFFFFFFFFFFFFULL};
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
    std::string data_dir{"data"};
    std::string config_path{};
    std::string genesis_path{};
};

// Operator's current public P2P (IPv4 only). Not a peer-count claim.
inline constexpr const char* kOperatorPublicP2p = "34.27.30.115:28545";

bool is_ipv4_endpoint(const std::string& endpoint);

const ChainConfig& default_config();
ChainConfig testnet_chain_config();
ChainConfig mainnet_chain_config();
NodeConfig default_node_config();

void set_runtime_network_mode(NetworkMode mode);
NetworkMode runtime_network_mode();
bool is_mainnet_runtime();
const char* network_mode_label(NetworkMode mode);
NetworkMode parse_network_mode(const std::string& value, bool& ok);
const char* pow_algorithm_label(PowAlgorithm algorithm);
PowAlgorithm parse_pow_algorithm(const std::string& value, bool& ok);

bool load_toml_config(const std::string& path, NodeConfig& cfg, std::string& error);
bool load_genesis_json(const std::string& path, ChainConfig& chain, std::string& error);
bool apply_cli_args(int argc, char** argv, NodeConfig& cfg, bool& show_help, std::string& error);
std::string daemon_help_text();

} // namespace addition
