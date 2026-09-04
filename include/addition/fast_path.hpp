#pragma once

#include "addition/config.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace addition {

// ADDITION_FAST_V1 is a separate high-throughput profile sketch.
// It must not pretend mainnet memory_hard PoW is Solana-speed.
// Full pipeline remains fail-closed until a later PR ships leader/execution.
//
// This slice fills beyond scaffold: explicit pipeline stages + typed messages
// with fail-closed validation. That is NOT a live product and does NOT set
// kFastPathPipelineShipped.

inline constexpr bool kFastPathPipelineShipped = false;

// Honest slice id for getinfo: typed stages exist; boot still refuse.
inline constexpr const char* kFastPathSliceId = "pipeline_stages_typed_v1";
inline constexpr const char* kFastPathDomainPrefix = "addition.fast_path_v1";

// Reject payloads that self-advertise a live / measured product.
inline constexpr const char* kFastFakeLiveMagic = "CLAIM_FAST_LIVE";
inline constexpr const char* kFastFakeTpsMagic = "CLAIM_MEASURED_TPS";

bool fast_path_pipeline_shipped();

// What this fill-PR actually delivered (not "shipped").
const char* fast_path_slice_label();

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

// Fail-closed readiness: false until full pipeline ships.
bool fast_path_boot_allowed(std::string& error);

// --- Pipeline stages + typed messages (REAL validation; not consensus) ---

enum class FastPipelineStage : std::uint8_t {
    Idle = 0,
    Ingested = 1,
    Scheduled = 2,
    Executed = 3,
    Verified = 4,
    Committed = 5,
};

enum class FastMessageKind : std::uint8_t {
    IngestBatch = 1,
    ScheduleTicket = 2,
    ExecutionReceipt = 3,
    VerifyAck = 4,
    CommitSeal = 5,
};

struct FastPipelineMessage {
    FastMessageKind kind{FastMessageKind::IngestBatch};
    std::uint64_t batch_id{0};
    std::string network_id;         // must be ADDITION_FAST_V1
    std::string body;               // canonical UTF-8 body (no fake TPS claims)
    std::string payload_digest_hex; // SHA3-512 hex of domain-separated preimage
};

const char* fast_pipeline_stage_label(FastPipelineStage stage);
const char* fast_message_kind_label(FastMessageKind kind);

// Expected stage after a successful apply of `kind` (from Idle upward).
FastPipelineStage fast_pipeline_stage_after(FastMessageKind kind);

// Kind required to leave `from` (fail-closed if none).
bool fast_pipeline_next_kind(FastPipelineStage from, FastMessageKind& kind_out, std::string& error);

// Domain-separated preimage for digest (does not include payload_digest_hex).
std::string fast_pipeline_digest_preimage(const FastPipelineMessage& msg);

// Compute SHA3-512 hex digest for a well-formed message body/kind/id.
std::string fast_pipeline_compute_digest_hex(const FastPipelineMessage& msg);

// Fill payload_digest_hex from kind/batch/network/body (helper for tests).
void fast_pipeline_seal_digest(FastPipelineMessage& msg);

bool fast_pipeline_validate_message(const FastPipelineMessage& msg, std::string& error);

// Local single-batch stage machine. Deterministic; not a leader or consensus.
class FastPipelineBatch {
public:
    explicit FastPipelineBatch(std::uint64_t batch_id);

    std::uint64_t batch_id() const { return batch_id_; }
    FastPipelineStage stage() const { return stage_; }

    // Apply one typed message. Fail-closed on wrong stage, digest, network, or magic.
    bool apply(const FastPipelineMessage& msg, std::string& error);

private:
    std::uint64_t batch_id_{0};
    FastPipelineStage stage_{FastPipelineStage::Idle};
};

// Enumerate legal stage order (for docs/tests).
std::vector<FastPipelineStage> fast_pipeline_stage_order();

} // namespace addition
