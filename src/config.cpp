#include "addition/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace addition {
namespace {

bool g_mode_explicit = false;
NetworkMode g_mode = NetworkMode::Testnet;

std::string trim_copy(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    if (i > 0) {
        s.erase(0, i);
    }
    return s;
}

std::string strip_comment(const std::string& line) {
    bool in_string = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string && c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

bool parse_quoted(const std::string& in, std::string& out) {
    if (in.size() < 2 || in.front() != '"' || in.back() != '"') {
        return false;
    }
    out = in.substr(1, in.size() - 2);
    return true;
}

bool parse_bool(const std::string& in, bool& out) {
    if (in == "true" || in == "1") {
        out = true;
        return true;
    }
    if (in == "false" || in == "0") {
        out = false;
        return true;
    }
    return false;
}

bool parse_u64(const std::string& in, std::uint64_t& out) {
    if (in.empty()) {
        return false;
    }
    try {
        std::size_t idx = 0;
        const unsigned long long v = std::stoull(in, &idx, 0);
        if (idx != in.size()) {
            return false;
        }
        out = static_cast<std::uint64_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_u16(const std::string& in, std::uint16_t& out) {
    std::uint64_t v = 0;
    if (!parse_u64(in, v) || v == 0 || v > 65535) {
        return false;
    }
    out = static_cast<std::uint16_t>(v);
    return true;
}

bool parse_string_array(const std::string& in, std::vector<std::string>& out) {
    const auto s = trim_copy(in);
    if (s.size() < 2 || s.front() != '[' || s.back() != ']') {
        return false;
    }
    out.clear();
    const std::string inner = s.substr(1, s.size() - 2);
    std::string cur;
    bool in_string = false;
    for (char c : inner) {
        if (c == '"') {
            in_string = !in_string;
            cur.push_back(c);
            continue;
        }
        if (c == ',' && !in_string) {
            const auto item = trim_copy(cur);
            if (!item.empty()) {
                std::string decoded;
                if (!parse_quoted(item, decoded)) {
                    return false;
                }
                out.push_back(decoded);
            }
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    const auto last = trim_copy(cur);
    if (!last.empty()) {
        std::string decoded;
        if (!parse_quoted(last, decoded)) {
            return false;
        }
        out.push_back(decoded);
    }
    return true;
}

void apply_network_profile_chain(ChainConfig& chain, NetworkMode mode) {
    const ChainConfig profile = (mode == NetworkMode::Testnet) ? testnet_chain_config() : mainnet_chain_config();
    chain.network_mode = profile.network_mode;
    chain.network_name = profile.network_name;
    chain.network_id = profile.network_id;
    chain.pow_algorithm = profile.pow_algorithm;
    chain.genesis_timestamp = profile.genesis_timestamp;
    chain.initial_difficulty_target = profile.initial_difficulty_target;
    chain.min_difficulty_target = profile.min_difficulty_target;
    chain.max_difficulty_target = profile.max_difficulty_target;
    chain.retarget_window = profile.retarget_window;
    chain.target_block_time_sec = profile.target_block_time_sec;
}

void apply_network_mode(NodeConfig& cfg, NetworkMode mode) {
    cfg.mode = mode;
    apply_network_profile_chain(cfg.chain, mode);
}

bool apply_kv(NodeConfig& cfg, const std::string& table, const std::string& key, const std::string& raw, std::string& error) {
    const std::string full = table.empty() ? key : table + "." + key;
    std::string text;
    const bool is_string = parse_quoted(raw, text);
    const std::string value = is_string ? text : raw;

    if (full == "network") {
        bool ok = false;
        const auto mode = parse_network_mode(value, ok);
        if (!ok) {
            error = "invalid network value: " + value;
            return false;
        }
        apply_network_mode(cfg, mode);
        return true;
    }
    if (full == "network_name" || full == "chain.network_name") {
        cfg.chain.network_name = value;
        return true;
    }
    if (full == "network_id" || full == "chain.network_id") {
        cfg.chain.network_id = value;
        return true;
    }
    if (full == "data_dir") {
        cfg.data_dir = value;
        return true;
    }
    if (full == "genesis_file" || full == "chain.genesis_file") {
        cfg.genesis_path = value;
        return true;
    }
    if (full == "bootstrap_peers" || full == "chain.bootstrap_peers") {
        std::vector<std::string> peers;
        if (!parse_string_array(raw, peers)) {
            error = "invalid bootstrap_peers array";
            return false;
        }
        for (const auto& peer : peers) {
            if (!is_ipv4_endpoint(peer)) {
                error = "bootstrap_peers must be IPv4 host:port (got " + peer + ")";
                return false;
            }
        }
        cfg.bootstrap_peers = peers;
        cfg.chain.bootstrap_peers = peers;
        return true;
    }
    if (full == "ports.local_rpc" || full == "local_rpc_port") {
        return parse_u16(value, cfg.local_rpc_port);
    }
    if (full == "ports.lan_rpc" || full == "lan_rpc_port") {
        return parse_u16(value, cfg.lan_rpc_port);
    }
    if (full == "ports.p2p" || full == "p2p_port") {
        return parse_u16(value, cfg.p2p_port);
    }
    if (full == "ports.public_rpc" || full == "public_rpc_port") {
        return parse_u16(value, cfg.public_rpc_port);
    }
    if (full == "ports.public_rpc_bind" || full == "public_rpc_bind") {
        cfg.public_rpc_bind = value;
        return true;
    }
    if (full == "advertised_p2p" || full == "ports.advertised_p2p") {
        if (value.empty()) {
            cfg.advertised_p2p.clear();
            return true;
        }
        return parse_advertised_p2p(value, cfg.advertised_p2p, error);
    }
    if (full == "enable_public_rpc" || full == "ports.enable_public_rpc") {
        return parse_bool(value, cfg.enable_public_rpc);
    }
    if (full == "enable_auto_mine" || full == "auto_mine") {
        return parse_bool(value, cfg.enable_auto_mine);
    }
    if (full == "auto_mine_interval_sec" || full == "auto_mine_interval") {
        std::uint64_t v = 0;
        if (!parse_u64(value, v) || v == 0 || v > 86400) {
            error = "invalid auto_mine_interval_sec (1-86400)";
            return false;
        }
        cfg.auto_mine_interval_sec = static_cast<std::uint32_t>(v);
        return true;
    }
    if (full == "auto_mine_reward") {
        if (value.empty()) {
            error = "auto_mine_reward must not be empty";
            return false;
        }
        cfg.auto_mine_reward = value;
        return true;
    }
    if (full == "chain.block_reward" || full == "block_reward") {
        return parse_u64(value, cfg.chain.block_reward);
    }
    if (full == "chain.max_supply" || full == "max_supply") {
        return parse_u64(value, cfg.chain.max_supply);
    }
    if (full == "chain.tail_emission_reward" || full == "tail_emission_reward") {
        return parse_u64(value, cfg.chain.tail_emission_reward);
    }
    if (full == "chain.genesis_timestamp" || full == "genesis_timestamp") {
        return parse_u64(value, cfg.chain.genesis_timestamp);
    }
    if (full == "chain.min_fee" || full == "min_fee") {
        return parse_u64(value, cfg.chain.min_fee);
    }
    if (full == "chain.target_block_time_sec" || full == "target_block_time_sec") {
        std::uint64_t v = 0;
        if (!parse_u64(value, v) || v == 0 || v > 86400) {
            return false;
        }
        cfg.chain.target_block_time_sec = static_cast<std::uint32_t>(v);
        return true;
    }
    if (full == "chain.halving_interval" || full == "halving_interval") {
        std::uint64_t v = 0;
        if (!parse_u64(value, v) || v == 0 || v > 0xFFFFFFFFULL) {
            return false;
        }
        cfg.chain.halving_interval = static_cast<std::uint32_t>(v);
        return true;
    }
    if (full == "chain.require_pq_signatures") {
        return parse_bool(value, cfg.chain.require_pq_signatures);
    }
    if (full == "chain.pow_algorithm" || full == "pow_algorithm") {
        bool ok = false;
        cfg.chain.pow_algorithm = parse_pow_algorithm(value, ok);
        if (!ok) {
            error = "invalid pow_algorithm (use sha3_512 or memory_hard)";
            return false;
        }
        return true;
    }
    if (full == "chain.initial_difficulty_target" || full == "initial_difficulty_target") {
        return parse_u64(value, cfg.chain.initial_difficulty_target);
    }
    if (full == "chain.min_difficulty_target" || full == "min_difficulty_target") {
        return parse_u64(value, cfg.chain.min_difficulty_target);
    }
    if (full == "chain.max_difficulty_target" || full == "max_difficulty_target") {
        return parse_u64(value, cfg.chain.max_difficulty_target);
    }
    if (full == "chain.retarget_window" || full == "retarget_window") {
        std::uint64_t v = 0;
        if (!parse_u64(value, v) || v == 0 || v > 0xFFFFFFFFULL) {
            return false;
        }
        cfg.chain.retarget_window = static_cast<std::uint32_t>(v);
        return true;
    }
    if (full == "comment" || full == "chain.comment") {
        return true;
    }
    error = "unknown config key: " + full;
    return false;
}

std::string read_balanced_value(std::istream& in, std::string first) {
    std::string v = first;
    int depth = 0;
    for (char c : v) {
        if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
        }
    }
    std::string extra;
    while (depth > 0 && std::getline(in, extra)) {
        v += extra;
        for (char c : extra) {
            if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            }
        }
    }
    return trim_copy(v);
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec);
}

std::string first_existing(const std::vector<std::string>& candidates) {
    for (const auto& c : candidates) {
        if (file_exists(c)) {
            return c;
        }
    }
    return {};
}

void apply_env_network_opt_in(NodeConfig& cfg) {
    if (const char* v = std::getenv("ADDITION_MAINNET_MODE")) {
        if (std::string(v) == "1") {
            apply_network_mode(cfg, NetworkMode::Mainnet);
        }
    }
}

} // namespace

bool is_ipv4_endpoint(const std::string& endpoint) {
    const auto colon = endpoint.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= endpoint.size()) {
        return false;
    }
    const std::string host = endpoint.substr(0, colon);
    const std::string port = endpoint.substr(colon + 1);
    if (host.empty() || port.empty()) {
        return false;
    }
    std::uint64_t port_n = 0;
    if (!parse_u64(port, port_n) || port_n == 0 || port_n > 65535) {
        return false;
    }

    int dots = 0;
    std::string octet;
    auto flush_octet = [&]() -> bool {
        if (octet.empty() || octet.size() > 3) {
            return false;
        }
        if (octet.size() > 1 && octet.front() == '0') {
            return false;
        }
        for (char c : octet) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        const int n = std::stoi(octet);
        return n >= 0 && n <= 255;
    };
    for (char c : host) {
        if (c == '.') {
            if (!flush_octet()) {
                return false;
            }
            octet.clear();
            ++dots;
            continue;
        }
        octet.push_back(c);
    }
    return dots == 3 && flush_octet();
}

bool is_loopback_host(const std::string& host) {
    return host == "127.0.0.1" || host == "0.0.0.0" || host == "localhost" || host == "::1";
}

bool is_loopback_endpoint(const std::string& endpoint) {
    const auto colon = endpoint.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    return is_loopback_host(endpoint.substr(0, colon));
}

bool is_self_peer_label(const std::string& endpoint) {
    if (endpoint == "self" || endpoint == "probe-self") {
        return true;
    }
    if (endpoint.rfind("self", 0) == 0 && !is_ipv4_endpoint(endpoint)) {
        return true;
    }
    if (endpoint.rfind("probe-self", 0) == 0) {
        return true;
    }
    return false;
}

bool is_external_advertised_peer(const std::string& endpoint) {
    return is_ipv4_endpoint(endpoint) && !is_loopback_endpoint(endpoint) && !is_self_peer_label(endpoint);
}

bool parse_advertised_p2p(const std::string& value, std::string& out, std::string& error) {
    const auto trimmed = trim_copy(value);
    if (trimmed.empty()) {
        out.clear();
        return true;
    }
    if (!is_external_advertised_peer(trimmed)) {
        error = "advertised_p2p must be a non-loopback IPv4 host:port (got " + trimmed + ")";
        return false;
    }
    out = trimmed;
    return true;
}

void apply_advertised_p2p_env(NodeConfig& cfg) {
    const char* raw = std::getenv("ADDITION_ADVERTISED_P2P");
    if (raw == nullptr) {
        return;
    }
    std::string error;
    if (!parse_advertised_p2p(raw, cfg.advertised_p2p, error)) {
        cfg.advertised_p2p.clear();
    }
}

std::vector<std::string> public_advertised_peers(const std::vector<std::string>& peers,
                                                 const std::string& advertised_p2p) {
    std::vector<std::string> out;
    for (const auto& peer : peers) {
        if (is_external_advertised_peer(peer) &&
            std::find(out.begin(), out.end(), peer) == out.end()) {
            out.push_back(peer);
        }
    }
    if (is_external_advertised_peer(advertised_p2p) &&
        std::find(out.begin(), out.end(), advertised_p2p) == out.end()) {
        out.push_back(advertised_p2p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

const ChainConfig& default_config() {
    static const ChainConfig cfg = testnet_chain_config();
    return cfg;
}

ChainConfig testnet_chain_config() {
    ChainConfig cfg{};
    cfg.network_mode = "testnet";
    cfg.network_name = kTestnetNetworkName;
    cfg.network_id = kTestnetNetworkId;
    cfg.genesis_timestamp = kTestnetGenesisTimestamp;
    cfg.bootstrap_peers = {"127.0.0.1:28545"};
    return cfg;
}

ChainConfig mainnet_chain_config() {
    ChainConfig cfg{};
    cfg.network_mode = "mainnet";
    cfg.network_name = kMainnetNetworkName;
    cfg.network_id = kMainnetNetworkId;
    cfg.genesis_timestamp = kMainnetGenesisTimestamp;
    cfg.max_supply = 50'000'000ULL;
    cfg.block_reward = 50ULL;
    cfg.tail_emission_reward = 1ULL;
    cfg.target_block_time_sec = 60U;
    cfg.difficulty_window = 120U;
    cfg.pow_algorithm = PowAlgorithm::MemoryHard;
    cfg.initial_difficulty_target = kMainnetDifficultyTarget;
    cfg.min_difficulty_target = kMainnetDifficultyTarget;
    cfg.max_difficulty_target = kMainnetMaxDifficultyTarget;
    cfg.retarget_window = 30U;
    cfg.halving_interval = 210000U;
    cfg.require_pq_signatures = true;
    cfg.require_privacy_pool = true;
    cfg.allow_zero_reward_blocks = true;
    cfg.min_fee = 1ULL;
    cfg.bootstrap_peers = {"127.0.0.1:28546"};
    return cfg;
}

NodeConfig default_node_config() {
    NodeConfig cfg{};
    cfg.mode = NetworkMode::Testnet;
    cfg.chain = testnet_chain_config();
    cfg.bootstrap_peers = cfg.chain.bootstrap_peers;
    return cfg;
}

NodeConfig mainnet_node_config() {
    NodeConfig cfg{};
    cfg.mode = NetworkMode::Mainnet;
    cfg.chain = mainnet_chain_config();
    cfg.local_rpc_port = 8546;
    cfg.lan_rpc_port = 18546;
    cfg.p2p_port = 28546;
    cfg.public_rpc_port = 38546;
    cfg.public_rpc_bind = "0.0.0.0";
    cfg.enable_public_rpc = false;
    cfg.enable_auto_mine = false;
    cfg.bootstrap_peers = cfg.chain.bootstrap_peers;
    cfg.data_dir = "data-mainnet";
    return cfg;
}

bool validate_network_profile(const NodeConfig& cfg, std::string& error) {
    error.clear();
    if (cfg.mode == NetworkMode::Mainnet) {
        if (cfg.chain.network_mode != "mainnet" || cfg.chain.network_id != kMainnetNetworkId) {
            error = "mainnet requires network_id=ADDITION_MAINNET_V1 and a mainnet genesis; "
                    "refusing to relabel a testnet chain";
            return false;
        }
        if (cfg.chain.genesis_timestamp == kTestnetGenesisTimestamp) {
            error = "mainnet genesis must not reuse the testnet genesis timestamp";
            return false;
        }
        if (cfg.chain.initial_difficulty_target > kTestnetHardDifficultyTarget ||
            cfg.chain.max_difficulty_target > kTestnetHardDifficultyTarget) {
            error = "mainnet difficulty must stay at or harder than the testnet hard target "
                    "(not the ~4ms easy target)";
            return false;
        }
        for (const auto& peer : cfg.bootstrap_peers) {
            if (peer == kOperatorPublicP2p) {
                error = "mainnet must not bootstrap the public testnet seed ";
                error += kOperatorPublicP2p;
                return false;
            }
        }
        return true;
    }
    if (cfg.chain.network_id == kMainnetNetworkId || cfg.chain.network_mode == "mainnet") {
        error = "testnet cannot load mainnet genesis (network_id=ADDITION_MAINNET_V1)";
        return false;
    }
    return true;
}

void set_runtime_network_mode(NetworkMode mode) {
    g_mode = mode;
    g_mode_explicit = true;
}

NetworkMode runtime_network_mode() {
    if (g_mode_explicit) {
        return g_mode;
    }
    if (const char* v = std::getenv("ADDITION_MAINNET_MODE")) {
        if (std::string(v) == "1") {
            return NetworkMode::Mainnet;
        }
    }
    return NetworkMode::Testnet;
}

bool is_mainnet_runtime() {
    switch (runtime_network_mode()) {
    case NetworkMode::Mainnet:
        return true;
    case NetworkMode::Testnet:
        return false;
    }
    return false;
}

const char* network_mode_label(NetworkMode mode) {
    switch (mode) {
    case NetworkMode::Testnet:
        return "testnet";
    case NetworkMode::Mainnet:
        return "mainnet";
    }
    return "testnet";
}

const char* pow_algorithm_label(PowAlgorithm algorithm) {
    switch (algorithm) {
    case PowAlgorithm::Sha3_512:
        return "sha3_512";
    case PowAlgorithm::MemoryHard:
        return "memory_hard";
    }
    const PowAlgorithm missing = algorithm;
    switch (missing) {
    case PowAlgorithm::Sha3_512:
    case PowAlgorithm::MemoryHard:
        break;
    }
    return "sha3_512";
}

PowAlgorithm parse_pow_algorithm(const std::string& value, bool& ok) {
    const auto v = trim_copy(value);
    if (v == "sha3_512" || v == "sha3-512" || v == "sha3") {
        ok = true;
        return PowAlgorithm::Sha3_512;
    }
    if (v == "memory_hard" || v == "memory-hard") {
        ok = true;
        return PowAlgorithm::MemoryHard;
    }
    ok = false;
    return PowAlgorithm::Sha3_512;
}

NetworkMode parse_network_mode(const std::string& value, bool& ok) {
    const auto v = trim_copy(value);
    if (v == "testnet" || v == "addition-testnet") {
        ok = true;
        return NetworkMode::Testnet;
    }
    if (v == "mainnet" || v == "addition-mainnet") {
        ok = true;
        return NetworkMode::Mainnet;
    }
    ok = false;
    return NetworkMode::Testnet;
}

bool load_toml_config(const std::string& path, NodeConfig& cfg, std::string& error) {
    error.clear();
    std::ifstream in(path);
    if (!in) {
        error = "cannot open config file: " + path;
        return false;
    }

    std::string table;
    std::string line;
    while (std::getline(in, line)) {
        line = trim_copy(strip_comment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            table = trim_copy(line.substr(1, line.size() - 2));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            error = "invalid config line: " + line;
            return false;
        }
        const auto key = trim_copy(line.substr(0, eq));
        auto raw = read_balanced_value(in, trim_copy(line.substr(eq + 1)));
        if (key.empty()) {
            error = "empty config key";
            return false;
        }
        if (!apply_kv(cfg, table, key, raw, error)) {
            if (error.empty()) {
                error = "invalid value for " + key;
            }
            return false;
        }
    }
    cfg.config_path = path;
    return true;
}

bool load_genesis_json(const std::string& path, ChainConfig& chain, std::string& error) {
    error.clear();
    std::ifstream in(path);
    if (!in) {
        error = "cannot open genesis file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    std::string key;
    std::string value;
    bool in_string = false;
    bool escaped = false;
    enum class State { SeekKey, InKey, SeekColon, SeekValue, InString, InToken };
    State state = State::SeekKey;

    auto apply_field = [&](const std::string& k, const std::string& v) -> bool {
        if (k == "network_mode") {
            chain.network_mode = v;
            return true;
        }
        if (k == "network_name") {
            chain.network_name = v;
            return true;
        }
        if (k == "network_id") {
            chain.network_id = v;
            return true;
        }
        if (k == "comment") {
            return true;
        }
        if (k == "genesis_timestamp") {
            return parse_u64(v, chain.genesis_timestamp);
        }
        if (k == "max_supply") {
            return parse_u64(v, chain.max_supply);
        }
        if (k == "block_reward") {
            return parse_u64(v, chain.block_reward);
        }
        if (k == "tail_emission_reward") {
            return parse_u64(v, chain.tail_emission_reward);
        }
        if (k == "min_fee") {
            return parse_u64(v, chain.min_fee);
        }
        if (k == "target_block_time_sec") {
            std::uint64_t n = 0;
            if (!parse_u64(v, n) || n == 0 || n > 86400) {
                return false;
            }
            chain.target_block_time_sec = static_cast<std::uint32_t>(n);
            return true;
        }
        if (k == "halving_interval") {
            std::uint64_t n = 0;
            if (!parse_u64(v, n) || n == 0 || n > 0xFFFFFFFFULL) {
                return false;
            }
            chain.halving_interval = static_cast<std::uint32_t>(n);
            return true;
        }
        if (k == "bootstrap_peers") {
            return parse_string_array(v, chain.bootstrap_peers);
        }
        if (k == "pow_algorithm") {
            bool ok = false;
            chain.pow_algorithm = parse_pow_algorithm(v, ok);
            return ok;
        }
        if (k == "initial_difficulty_target") {
            return parse_u64(v, chain.initial_difficulty_target);
        }
        if (k == "min_difficulty_target") {
            return parse_u64(v, chain.min_difficulty_target);
        }
        if (k == "max_difficulty_target") {
            return parse_u64(v, chain.max_difficulty_target);
        }
        if (k == "retarget_window") {
            std::uint64_t n = 0;
            if (!parse_u64(v, n) || n == 0 || n > 0xFFFFFFFFULL) {
                return false;
            }
            chain.retarget_window = static_cast<std::uint32_t>(n);
            return true;
        }
        error = "unknown genesis key: " + k;
        return false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (state == State::SeekKey) {
            if (c == '"') {
                key.clear();
                state = State::InKey;
            }
            continue;
        }
        if (state == State::InKey) {
            if (c == '"') {
                state = State::SeekColon;
            } else {
                key.push_back(c);
            }
            continue;
        }
        if (state == State::SeekColon) {
            if (c == ':') {
                state = State::SeekValue;
            }
            continue;
        }
        if (state == State::SeekValue) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                continue;
            }
            value.clear();
            if (c == '"') {
                in_string = true;
                escaped = false;
                state = State::InString;
                continue;
            }
            value.push_back(c);
            state = State::InToken;
            continue;
        }
        if (state == State::InString) {
            if (escaped) {
                value.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') {
                if (!apply_field(key, value)) {
                    return false;
                }
                state = State::SeekKey;
            } else {
                value.push_back(c);
            }
            continue;
        }
        if (state == State::InToken) {
            if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c))) {
                if (!apply_field(key, trim_copy(value))) {
                    return false;
                }
                state = State::SeekKey;
                if (c == '"') {
                    // keep parser moving; next loop handles keys
                }
            } else {
                value.push_back(c);
            }
        }
        (void)in_string;
    }
    return true;
}

bool apply_cli_args(int argc, char** argv, NodeConfig& cfg, bool& show_help, std::string& error) {
    show_help = false;
    error.clear();
    cfg = default_node_config();
    apply_env_network_opt_in(cfg);

    std::string cli_network;
    std::string cli_config;
    std::string cli_genesis;
    std::string cli_data_dir;
    std::string cli_public_rpc_port;
    std::string cli_public_rpc_bind;
    std::string cli_local_rpc_port;
    std::string cli_p2p_port;
    std::vector<std::string> cli_bootstrap;
    std::string cli_auto_mine_interval;
    std::string cli_auto_mine_reward;
    bool network_from_cli = false;
    bool public_rpc_from_cli = false;
    bool auto_mine_from_cli = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        auto take_value = [&](std::string& dest) -> bool {
            if (i + 1 >= argc) {
                error = "missing value for " + arg;
                return false;
            }
            dest = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            show_help = true;
            return true;
        }
        if (arg == "--network") {
            if (!take_value(cli_network)) {
                return false;
            }
            network_from_cli = true;
            continue;
        }
        if (arg.rfind("--network=", 0) == 0) {
            cli_network = arg.substr(10);
            network_from_cli = true;
            continue;
        }
        if (arg == "--mainnet") {
            cli_network = "mainnet";
            network_from_cli = true;
            continue;
        }
        if (arg == "--config") {
            if (!take_value(cli_config)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--config=", 0) == 0) {
            cli_config = arg.substr(9);
            continue;
        }
        if (arg == "--genesis") {
            if (!take_value(cli_genesis)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--genesis=", 0) == 0) {
            cli_genesis = arg.substr(10);
            continue;
        }
        if (arg == "--data-dir") {
            if (!take_value(cli_data_dir)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--data-dir=", 0) == 0) {
            cli_data_dir = arg.substr(11);
            continue;
        }
        if (arg == "--public-rpc") {
            public_rpc_from_cli = true;
            continue;
        }
        if (arg == "--public-rpc-port") {
            if (!take_value(cli_public_rpc_port)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--public-rpc-port=", 0) == 0) {
            cli_public_rpc_port = arg.substr(18);
            continue;
        }
        if (arg == "--public-rpc-bind") {
            if (!take_value(cli_public_rpc_bind)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--public-rpc-bind=", 0) == 0) {
            cli_public_rpc_bind = arg.substr(18);
            continue;
        }
        if (arg == "--local-rpc-port") {
            if (!take_value(cli_local_rpc_port)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--local-rpc-port=", 0) == 0) {
            cli_local_rpc_port = arg.substr(17);
            continue;
        }
        if (arg == "--p2p-port") {
            if (!take_value(cli_p2p_port)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--p2p-port=", 0) == 0) {
            cli_p2p_port = arg.substr(11);
            continue;
        }
        if (arg == "--bootstrap") {
            std::string peer;
            if (!take_value(peer)) {
                return false;
            }
            cli_bootstrap.push_back(std::move(peer));
            continue;
        }
        if (arg.rfind("--bootstrap=", 0) == 0) {
            cli_bootstrap.push_back(arg.substr(12));
            continue;
        }
        if (arg == "--auto-mine") {
            auto_mine_from_cli = true;
            continue;
        }
        if (arg == "--auto-mine-interval") {
            if (!take_value(cli_auto_mine_interval)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--auto-mine-interval=", 0) == 0) {
            cli_auto_mine_interval = arg.substr(21);
            continue;
        }
        if (arg == "--auto-mine-reward") {
            if (!take_value(cli_auto_mine_reward)) {
                return false;
            }
            continue;
        }
        if (arg.rfind("--auto-mine-reward=", 0) == 0) {
            cli_auto_mine_reward = arg.substr(19);
            continue;
        }
        error = "unknown argument: " + arg;
        return false;
    }

    NetworkMode mode = NetworkMode::Testnet;
    if (network_from_cli) {
        bool ok = false;
        mode = parse_network_mode(cli_network, ok);
        if (!ok) {
            error = "invalid --network value (use testnet, mainnet, or --mainnet)";
            return false;
        }
    } else if (const char* v = std::getenv("ADDITION_MAINNET_MODE")) {
        if (std::string(v) == "1") {
            mode = NetworkMode::Mainnet;
        }
    }
    cfg = (mode == NetworkMode::Mainnet) ? mainnet_node_config() : default_node_config();

    std::string config_path = cli_config;
    if (config_path.empty()) {
        if (mode == NetworkMode::Mainnet) {
            config_path = first_existing({"config-mainnet.toml", "../config-mainnet.toml"});
        } else {
            config_path = first_existing({"config.toml", "../config.toml"});
        }
    } else if (!file_exists(config_path)) {
        error = "config file not found: " + config_path;
        return false;
    }
    if (!config_path.empty()) {
        if (!load_toml_config(config_path, cfg, error)) {
            return false;
        }
    }

    std::string genesis_path = cli_genesis;
    if (genesis_path.empty()) {
        if (!cfg.genesis_path.empty() && file_exists(cfg.genesis_path)) {
            genesis_path = cfg.genesis_path;
        } else if (mode == NetworkMode::Mainnet) {
            genesis_path = first_existing({"genesis-mainnet.json", "../genesis-mainnet.json"});
        } else {
            genesis_path = first_existing({"genesis.json", "../genesis.json"});
        }
    } else if (!file_exists(genesis_path)) {
        error = "genesis file not found: " + genesis_path;
        return false;
    }
    if (!genesis_path.empty()) {
        if (!load_genesis_json(genesis_path, cfg.chain, error)) {
            return false;
        }
        cfg.genesis_path = genesis_path;
        if (!cfg.chain.bootstrap_peers.empty()) {
            cfg.bootstrap_peers = cfg.chain.bootstrap_peers;
        }
    }

    if (!cli_data_dir.empty()) {
        cfg.data_dir = cli_data_dir;
    }
    if (public_rpc_from_cli) {
        cfg.enable_public_rpc = true;
    }
    if (!cli_public_rpc_port.empty()) {
        if (!parse_u16(cli_public_rpc_port, cfg.public_rpc_port)) {
            error = "invalid --public-rpc-port";
            return false;
        }
    }
    if (!cli_public_rpc_bind.empty()) {
        cfg.public_rpc_bind = cli_public_rpc_bind;
    }
    if (!cli_local_rpc_port.empty()) {
        if (!parse_u16(cli_local_rpc_port, cfg.local_rpc_port)) {
            error = "invalid --local-rpc-port";
            return false;
        }
    }
    if (!cli_p2p_port.empty()) {
        if (!parse_u16(cli_p2p_port, cfg.p2p_port)) {
            error = "invalid --p2p-port";
            return false;
        }
    }
    if (!cli_bootstrap.empty()) {
        for (const auto& peer : cli_bootstrap) {
            if (!is_ipv4_endpoint(peer)) {
                error = "invalid --bootstrap (IPv4 host:port only): " + peer;
                return false;
            }
        }
        cfg.bootstrap_peers = cli_bootstrap;
        cfg.chain.bootstrap_peers = cli_bootstrap;
    }
    if (auto_mine_from_cli) {
        cfg.enable_auto_mine = true;
    }
    if (!cli_auto_mine_interval.empty()) {
        std::uint64_t v = 0;
        if (!parse_u64(cli_auto_mine_interval, v) || v == 0 || v > 86400) {
            error = "invalid --auto-mine-interval (1-86400 seconds)";
            return false;
        }
        cfg.auto_mine_interval_sec = static_cast<std::uint32_t>(v);
    }
    if (!cli_auto_mine_reward.empty()) {
        cfg.auto_mine_reward = cli_auto_mine_reward;
    }

    cfg.mode = mode;
    if (cfg.bootstrap_peers.empty()) {
        cfg.bootstrap_peers = (mode == NetworkMode::Mainnet)
                                  ? std::vector<std::string>{"127.0.0.1:28546"}
                                  : std::vector<std::string>{"127.0.0.1:28545"};
        cfg.chain.bootstrap_peers = cfg.bootstrap_peers;
    }
    for (const auto& peer : cfg.bootstrap_peers) {
        if (!is_ipv4_endpoint(peer)) {
            error = "bootstrap_peers must be IPv4 host:port (got " + peer + ")";
            return false;
        }
    }
    cfg.chain.bootstrap_peers = cfg.bootstrap_peers;
    if (const char* raw = std::getenv("ADDITION_ADVERTISED_P2P")) {
        if (!parse_advertised_p2p(raw, cfg.advertised_p2p, error)) {
            return false;
        }
    }
    if (!validate_network_profile(cfg, error)) {
        return false;
    }
    return true;
}

std::string daemon_help_text() {
    return "ADDITION research daemon (testnet by default; --mainnet is a separate local chain, not a live public network)\n"
           "\n"
           "Usage:\n"
           "  additiond [--network testnet|mainnet] [--mainnet] [--config PATH] [--genesis PATH] [--data-dir PATH]\n"
           "            [--public-rpc] [--public-rpc-port PORT] [--public-rpc-bind IP]\n"
           "            [--local-rpc-port PORT] [--p2p-port PORT] [--bootstrap IP:PORT]\n"
           "            [--auto-mine] [--auto-mine-interval SEC] [--auto-mine-reward ADDR]\n"
           "\n"
           "Defaults:\n"
           "  --network testnet\n"
           "  --config  config.toml (testnet) or config-mainnet.toml (--mainnet)\n"
           "  --genesis genesis.json (testnet) or genesis-mainnet.json (--mainnet)\n"
           "  --data-dir data (testnet) or data-mainnet (--mainnet)\n"
           "\n"
           "--mainnet (same as --network mainnet) starts ADDITION_MAINNET_V1 from genesis-mainnet.json.\n"
           "  It is a separate chain, not a live public network, and must not use the testnet seed.\n"
           "  Default write RPC is 127.0.0.1:8546; public-read default is :38546; P2P default :28546 (off).\n"
           "\n"
           "Local trusted write RPC: 127.0.0.1 (never bound to 0.0.0.0; optional ADDITION_RPC_TOKEN)\n"
           "  Override port with --local-rpc-port or ADDITION_LOCAL_RPC_PORT (second node: 8546).\n"
           "Public read RPC (opt-in, allowlist only):\n"
           "  additiond --network testnet --public-rpc\n"
           "  or ADDITION_ENABLE_PUBLIC_RPC=1\n"
           "  bind 0.0.0.0:38545 by default (ADDITION_PUBLIC_RPC_PORT / ADDITION_PUBLIC_RPC_BIND)\n"
           "  use --public-rpc-bind 127.0.0.1 for a local tunnel\n"
           "  allowlist: getinfo monetary_info crypto_selftest tx_status peers getblock getblockhash getblockraw\n"
           "  TCP:  printf 'getinfo\\n' | nc HOST 38545\n"
           "  HTTP: curl 'http://HOST:38545/rpc?cmd=getinfo'\n"
           "LAN RPC stays token-gated (ADDITION_ENABLE_LAN_RPC=1 + ADDITION_LAN_RPC_TOKEN).\n"
           "P2P stays off unless ADDITION_ENABLE_P2P_RPC=1. --p2p-port / ADDITION_P2P_PORT (second node: 28546).\n"
           "--bootstrap IP:PORT replaces bootstrap_peers (IPv4 only; hostnames are not resolved).\n"
           "  Operator public P2P (current): 34.27.30.115:28545 — one IPv4 peer, not a peer list.\n"
           "  Seed operators set ADDITION_ADVERTISED_P2P=34.27.30.115:28545 so public getinfo/peers\n"
           "  do not list self. Public TCP 28545 can timeout or be filtered; HTTP :80 sync is the\n"
           "  reliable join path. Write RPC stays 127.0.0.1.\n"
           "Auto-mine (testnet only, off by default, never on public RPC):\n"
           "  --auto-mine or ADDITION_AUTO_MINE=1\n"
           "  --auto-mine-interval SEC or ADDITION_AUTO_MINE_INTERVAL (default 60)\n"
           "  --auto-mine-reward ADDR or ADDITION_AUTO_MINE_REWARD (default miner1)\n"
           "  After N seconds the daemon mines one block in-process and persists blocks.dat.\n"
           "  Refused on --network mainnet even if the flag is set.\n"
           "Do not expose write RPC to the world.\n"
           "\n"
           "Local wallet (trusted RPC only): createwallet [name] [scheme], wallet_list, wallet_info,\n"
           "  wallet_balance, wallet_send, wallet_sign. Keys stay in <data-dir>/wallets/*.wal.\n"
           "  Bitcoin-like user model (keys, UTXOs, send/receive, fee). Not BIP-compatible.\n"
           "Bitcoin UTXO hygiene (trusted RPC only): hygiene_classify [path], hygiene_attest,\n"
           "  hygiene_verify. Signed ADDITION receipt; moves_bitcoin=0; not BIP-360.\n"
           "\n"
           "This is a research prototype / testnet. It does not claim to be a live mainnet.\n";
}

} // namespace addition
