#include "addition/fast_path.hpp"

#include <sstream>

namespace addition {

bool fast_path_pipeline_shipped() {
    return kFastPathPipelineShipped;
}

const char* consensus_path_label(const ChainConfig& cfg) {
    if (cfg.network_id == kFastNetworkId || cfg.network_mode == "fast") {
        return "leader_pipeline_scaffold";
    }
    if (cfg.network_id == kMainnetNetworkId || cfg.network_mode == "mainnet" ||
        cfg.pow_algorithm == PowAlgorithm::MemoryHard) {
        return "memory_hard_pow";
    }
    if (cfg.network_id == kRegtestNetworkId || cfg.network_mode == "regtest") {
        return "sha3_header_pow_regtest";
    }
    return "sha3_header_pow";
}

const char* fast_path_status_label(const ChainConfig& cfg) {
    if (cfg.network_id == kFastNetworkId || cfg.network_mode == "fast") {
        if (fast_path_pipeline_shipped()) {
            return "shipped";
        }
        return "scaffold_incomplete";
    }
    return "not_this_network";
}

const char* throughput_claim_label() {
    return "none";
}

std::string fast_path_info_fields(const ChainConfig& cfg) {
    std::ostringstream out;
    out << " consensus_path=" << consensus_path_label(cfg)
        << " fast_path_network_id=" << kFastNetworkId
        << " fast_path_status=" << fast_path_status_label(cfg)
        << " fast_path_shipped=" << (fast_path_pipeline_shipped() ? "true" : "false")
        << " throughput_claim=" << throughput_claim_label();
    return out.str();
}

bool fast_path_boot_allowed(std::string& error) {
    if (fast_path_pipeline_shipped()) {
        error.clear();
        return true;
    }
    error = "ADDITION_FAST_V1 scaffold incomplete: leader/pipeline/execution not shipped; "
            "refusing to boot (fail-closed). Live public product is --mainnet "
            "ADDITION_MAINNET_V1 memory_hard at 0x000000FFFFFFFFFF. See docs/FAST_PATH_V1.md";
    return false;
}

} // namespace addition
