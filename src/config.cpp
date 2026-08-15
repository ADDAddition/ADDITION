#include "addition/config.hpp"

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

void apply_network_mode(NodeConfig& cfg, NetworkMode mode) {
    cfg.mode = mode;
    if (mode == NetworkMode::Testnet) {
        cfg.chain.network_mode = "testnet";
        if (cfg.chain.network_name.empty() || cfg.chain.network_name == "mainnet") {
            cfg.chain.network_name = "addition-testnet";
        }
        if (cfg.chain.network_id.empty() || cfg.chain.network_id == "ADDITION_MAINNET_V1") {
            cfg.chain.network_id = "ADDITION_TESTNET_V1";
        }
        return;
    }
    cfg.chain.network_mode = "mainnet";
    if (cfg.chain.network_name.empty() || cfg.chain.network_name == "addition-testnet") {
        cfg.chain.network_name = "mainnet";
    }
    if (cfg.chain.network_id.empty() || cfg.chain.network_id == "ADDITION_TESTNET_V1") {
        cfg.chain.network_id = "ADDITION_MAINNET_V1";
    }
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
    if (full == "enable_public_rpc" || full == "ports.enable_public_rpc") {
        return parse_bool(value, cfg.enable_public_rpc);
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

const ChainConfig& default_config() {
    static const ChainConfig cfg = testnet_chain_config();
    return cfg;
}

ChainConfig testnet_chain_config() {
    ChainConfig cfg{};
    cfg.network_mode = "testnet";
    cfg.network_name = "addition-testnet";
    cfg.network_id = "ADDITION_TESTNET_V1";
    cfg.bootstrap_peers = {"127.0.0.1:28545"};
    return cfg;
}

ChainConfig mainnet_chain_config() {
    ChainConfig cfg = testnet_chain_config();
    cfg.network_mode = "mainnet";
    cfg.network_name = "mainnet";
    cfg.network_id = "ADDITION_MAINNET_V1";
    return cfg;
}

NodeConfig default_node_config() {
    NodeConfig cfg{};
    cfg.mode = NetworkMode::Testnet;
    cfg.chain = testnet_chain_config();
    cfg.bootstrap_peers = cfg.chain.bootstrap_peers;
    return cfg;
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

NetworkMode parse_network_mode(const std::string& value, bool& ok) {
    const auto v = trim_copy(value);
    if (v == "testnet" || v == "addition-testnet") {
        ok = true;
        return NetworkMode::Testnet;
    }
    if (v == "mainnet") {
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
    bool network_from_cli = false;
    bool public_rpc_from_cli = false;

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
        error = "unknown argument: " + arg;
        return false;
    }

    std::string config_path = cli_config;
    if (config_path.empty()) {
        config_path = first_existing({"config.toml", "../config.toml"});
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

    if (network_from_cli) {
        bool ok = false;
        const auto mode = parse_network_mode(cli_network, ok);
        if (!ok) {
            error = "invalid --network value (use testnet or mainnet)";
            return false;
        }
        apply_network_mode(cfg, mode);
    }

    if (cfg.bootstrap_peers.empty()) {
        cfg.bootstrap_peers = {"127.0.0.1:28545"};
        cfg.chain.bootstrap_peers = cfg.bootstrap_peers;
    }
    return true;
}

std::string daemon_help_text() {
    return "ADDITION research daemon (testnet by default; not a live mainnet)\n"
           "\n"
           "Usage:\n"
           "  additiond [--network testnet|mainnet] [--config PATH] [--genesis PATH] [--data-dir PATH]\n"
           "            [--public-rpc] [--public-rpc-port PORT] [--public-rpc-bind IP]\n"
           "\n"
           "Defaults:\n"
           "  --network testnet\n"
           "  --config  config.toml (if present)\n"
           "  --genesis genesis.json (if present)\n"
           "\n"
           "Local trusted RPC: 127.0.0.1:8545 (all commands; optional ADDITION_RPC_TOKEN)\n"
           "Public read RPC (opt-in, allowlist only):\n"
           "  additiond --network testnet --public-rpc\n"
           "  or ADDITION_ENABLE_PUBLIC_RPC=1\n"
           "  bind 0.0.0.0:38545 by default (ADDITION_PUBLIC_RPC_PORT / ADDITION_PUBLIC_RPC_BIND)\n"
           "  allowlist: getinfo monetary_info crypto_selftest tx_status peers getblock getblockhash\n"
           "  TCP:  printf 'getinfo\\n' | nc HOST 38545\n"
           "  HTTP: curl 'http://HOST:38545/rpc?cmd=getinfo'\n"
           "LAN RPC stays token-gated (ADDITION_ENABLE_LAN_RPC=1 + ADDITION_LAN_RPC_TOKEN).\n"
           "P2P stays off unless ADDITION_ENABLE_P2P_RPC=1. Do not expose write RPC to the world.\n"
           "\n"
           "Local wallet (trusted RPC only): createwallet [name], wallet_list, wallet_info,\n"
           "  wallet_balance, wallet_send, wallet_sign. Keys stay in <data-dir>/wallets/*.wal.\n"
           "  Bitcoin-like user model (keys, UTXOs, send/receive, fee). Not BIP-compatible.\n"
           "\n"
           "This is a research prototype / testnet. It does not claim to be a live mainnet.\n";
}

} // namespace addition
