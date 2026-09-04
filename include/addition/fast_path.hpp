#pragma once

#include "addition/config.hpp"

#include <string>

namespace addition {

// ADDITION_FAST_V1 is a separate high-throughput profile sketch.
// It must not pretend mainnet memory_hard PoW is Solana-speed.
// Pipeline is fail-closed until a later PR ships leader/execution.

inline constexpr bool kFastPathPipelineShipped = false;

bool fast_path_pipeline_shipped();

// consensus_path labels for getinfo (honest, not a TPS claim).
const char* consensus_path_label(const ChainConfig& cfg);

// fast_path_status for getinfo.
// - not_this_network: running mainnet/testnet/regtest
// - scaffold_incomplete: ADDITION_FAST_V1 while pipeline not shipped
// - shipped: reserved; forbidden while kFastPathPipelineShipped==false
const char* fast_path_status_label(const ChainConfig& cfg);

// Always "none" until a real measured fast-path bench exists.
const char* throughput_claim_label();

// Space-prefixed getinfo / protocol_status fields. Never invents Solana TPS.
std::string fast_path_info_fields(const ChainConfig& cfg);

// Fail-closed readiness: false until pipeline ships.
bool fast_path_boot_allowed(std::string& error);

} // namespace addition
