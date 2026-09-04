#include "addition/fast_path.hpp"

#include "addition/crypto.hpp"

#include <cctype>
#include <sstream>

namespace addition {
namespace {

bool is_hex_even(const std::string& s) {
    if (s.empty() || (s.size() % 2) != 0) {
        return false;
    }
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool contains_ascii_ci(const std::string& hay, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return false;
    }
    std::string lower_hay;
    lower_hay.reserve(hay.size());
    for (char c : hay) {
        lower_hay.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    std::string lower_needle;
    for (const char* p = needle; *p != '\0'; ++p) {
        lower_needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return lower_hay.find(lower_needle) != std::string::npos;
}

bool looks_like_fake_fast_claim(const std::string& body) {
    if (body.find(kFastFakeLiveMagic) != std::string::npos) {
        return true;
    }
    if (body.find(kFastFakeTpsMagic) != std::string::npos) {
        return true;
    }
    // Hex encoding of CLAIM_FAST_LIVE / CLAIM_MEASURED_TPS prefixes.
    static const char* kLiveHex = "434c41494d5f464153545f4c495645";
    static const char* kTpsHex = "434c41494d5f4d454153555245445f545053";
    if (contains_ascii_ci(body, kLiveHex) || contains_ascii_ci(body, kTpsHex)) {
        return true;
    }
    // Refuse invented Solana/measured TPS slogans in research bodies.
    if (contains_ascii_ci(body, "solana_tps") || contains_ascii_ci(body, "measured_solana") ||
        contains_ascii_ci(body, "throughput_claim=solana") ||
        contains_ascii_ci(body, "objective_tps_ok")) {
        return true;
    }
    return false;
}

} // namespace

bool fast_path_pipeline_shipped() {
    return kFastPathPipelineShipped;
}

const char* fast_path_slice_label() {
    return kFastPathSliceId;
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
        << " fast_path_slice=" << fast_path_slice_label()
        << " throughput_claim=" << throughput_claim_label();
    return out.str();
}

bool fast_path_boot_allowed(std::string& error) {
    if (fast_path_pipeline_shipped()) {
        error.clear();
        return true;
    }
    error = "ADDITION_FAST_V1 scaffold incomplete: leader/pipeline/execution not shipped; "
            "refusing to boot (fail-closed). Typed stages (" +
            std::string(fast_path_slice_label()) +
            ") exist for research only. Live public product is --mainnet "
            "ADDITION_MAINNET_V1 memory_hard at 0x000000FFFFFFFFFF. See docs/FAST_PATH_V1.md";
    return false;
}

const char* fast_pipeline_stage_label(FastPipelineStage stage) {
    switch (stage) {
        case FastPipelineStage::Idle:
            return "idle";
        case FastPipelineStage::Ingested:
            return "ingested";
        case FastPipelineStage::Scheduled:
            return "scheduled";
        case FastPipelineStage::Executed:
            return "executed";
        case FastPipelineStage::Verified:
            return "verified";
        case FastPipelineStage::Committed:
            return "committed";
    }
    const FastPipelineStage missing = stage;
    switch (missing) {
        case FastPipelineStage::Idle:
        case FastPipelineStage::Ingested:
        case FastPipelineStage::Scheduled:
        case FastPipelineStage::Executed:
        case FastPipelineStage::Verified:
        case FastPipelineStage::Committed:
            break;
    }
    return "unknown";
}

const char* fast_message_kind_label(FastMessageKind kind) {
    switch (kind) {
        case FastMessageKind::IngestBatch:
            return "ingest_batch";
        case FastMessageKind::ScheduleTicket:
            return "schedule_ticket";
        case FastMessageKind::ExecutionReceipt:
            return "execution_receipt";
        case FastMessageKind::VerifyAck:
            return "verify_ack";
        case FastMessageKind::CommitSeal:
            return "commit_seal";
    }
    const FastMessageKind missing = kind;
    switch (missing) {
        case FastMessageKind::IngestBatch:
        case FastMessageKind::ScheduleTicket:
        case FastMessageKind::ExecutionReceipt:
        case FastMessageKind::VerifyAck:
        case FastMessageKind::CommitSeal:
            break;
    }
    return "unknown";
}

FastPipelineStage fast_pipeline_stage_after(FastMessageKind kind) {
    switch (kind) {
        case FastMessageKind::IngestBatch:
            return FastPipelineStage::Ingested;
        case FastMessageKind::ScheduleTicket:
            return FastPipelineStage::Scheduled;
        case FastMessageKind::ExecutionReceipt:
            return FastPipelineStage::Executed;
        case FastMessageKind::VerifyAck:
            return FastPipelineStage::Verified;
        case FastMessageKind::CommitSeal:
            return FastPipelineStage::Committed;
    }
    const FastMessageKind missing = kind;
    switch (missing) {
        case FastMessageKind::IngestBatch:
        case FastMessageKind::ScheduleTicket:
        case FastMessageKind::ExecutionReceipt:
        case FastMessageKind::VerifyAck:
        case FastMessageKind::CommitSeal:
            break;
    }
    return FastPipelineStage::Idle;
}

bool fast_pipeline_next_kind(FastPipelineStage from, FastMessageKind& kind_out, std::string& error) {
    switch (from) {
        case FastPipelineStage::Idle:
            kind_out = FastMessageKind::IngestBatch;
            error.clear();
            return true;
        case FastPipelineStage::Ingested:
            kind_out = FastMessageKind::ScheduleTicket;
            error.clear();
            return true;
        case FastPipelineStage::Scheduled:
            kind_out = FastMessageKind::ExecutionReceipt;
            error.clear();
            return true;
        case FastPipelineStage::Executed:
            kind_out = FastMessageKind::VerifyAck;
            error.clear();
            return true;
        case FastPipelineStage::Verified:
            kind_out = FastMessageKind::CommitSeal;
            error.clear();
            return true;
        case FastPipelineStage::Committed:
            error = "fast_pipeline: batch already committed; no further stage";
            return false;
    }
    const FastPipelineStage missing = from;
    switch (missing) {
        case FastPipelineStage::Idle:
        case FastPipelineStage::Ingested:
        case FastPipelineStage::Scheduled:
        case FastPipelineStage::Executed:
        case FastPipelineStage::Verified:
        case FastPipelineStage::Committed:
            break;
    }
    error = "fast_pipeline: unknown stage";
    return false;
}

std::string fast_pipeline_digest_preimage(const FastPipelineMessage& msg) {
    std::ostringstream oss;
    oss << kFastPathDomainPrefix << '|' << fast_message_kind_label(msg.kind) << '|' << msg.batch_id
        << '|' << msg.network_id << '|' << msg.body;
    return oss.str();
}

std::string fast_pipeline_compute_digest_hex(const FastPipelineMessage& msg) {
    return to_hex(sha3_512_bytes(fast_pipeline_digest_preimage(msg)));
}

void fast_pipeline_seal_digest(FastPipelineMessage& msg) {
    msg.payload_digest_hex = fast_pipeline_compute_digest_hex(msg);
}

bool fast_pipeline_validate_message(const FastPipelineMessage& msg, std::string& error) {
    if (msg.batch_id == 0) {
        error = "fast_pipeline: batch_id must be > 0";
        return false;
    }
    if (msg.network_id != kFastNetworkId) {
        error = "fast_pipeline: network_id must be ADDITION_FAST_V1 (fail-closed)";
        return false;
    }
    if (msg.body.empty()) {
        error = "fast_pipeline: body must be non-empty";
        return false;
    }
    if (looks_like_fake_fast_claim(msg.body)) {
        error = "fast_pipeline: body contains forbidden live/TPS claim magic (fail-closed)";
        return false;
    }
    if (!is_hex_even(msg.payload_digest_hex) || msg.payload_digest_hex.size() != 128) {
        error = "fast_pipeline: payload_digest_hex must be 128 hex chars (SHA3-512)";
        return false;
    }
    const auto expected = fast_pipeline_compute_digest_hex(msg);
    // Compare case-insensitively so uppercase hex seals still validate.
    std::string got_lower;
    got_lower.reserve(msg.payload_digest_hex.size());
    for (char c : msg.payload_digest_hex) {
        got_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (got_lower != expected) {
        error = "fast_pipeline: payload_digest_hex mismatch (SHA3-512 fail-closed)";
        return false;
    }
    // Kind label must be known (rejects garbage cast values via unknown path).
    if (std::string(fast_message_kind_label(msg.kind)) == "unknown") {
        error = "fast_pipeline: unknown message kind";
        return false;
    }
    error.clear();
    return true;
}

FastPipelineBatch::FastPipelineBatch(std::uint64_t batch_id) : batch_id_(batch_id) {}

bool FastPipelineBatch::apply(const FastPipelineMessage& msg, std::string& error) {
    if (batch_id_ == 0) {
        error = "fast_pipeline: batch constructed with batch_id=0";
        return false;
    }
    if (msg.batch_id != batch_id_) {
        error = "fast_pipeline: message batch_id does not match machine";
        return false;
    }
    if (!fast_pipeline_validate_message(msg, error)) {
        return false;
    }
    FastMessageKind expected_kind{};
    if (!fast_pipeline_next_kind(stage_, expected_kind, error)) {
        return false;
    }
    if (msg.kind != expected_kind) {
        std::ostringstream oss;
        oss << "fast_pipeline: expected kind=" << fast_message_kind_label(expected_kind)
            << " at stage=" << fast_pipeline_stage_label(stage_)
            << "; got kind=" << fast_message_kind_label(msg.kind) << " (fail-closed)";
        error = oss.str();
        return false;
    }
    stage_ = fast_pipeline_stage_after(msg.kind);
    error.clear();
    return true;
}

std::vector<FastPipelineStage> fast_pipeline_stage_order() {
    return {
        FastPipelineStage::Idle,
        FastPipelineStage::Ingested,
        FastPipelineStage::Scheduled,
        FastPipelineStage::Executed,
        FastPipelineStage::Verified,
        FastPipelineStage::Committed,
    };
}

} // namespace addition
