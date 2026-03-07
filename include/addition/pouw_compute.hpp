#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace addition {

class PoUWComputeEngine {
public:
    struct Job {
        std::string job_id;
        std::string requester_addr;
        std::string job_type;
        std::string input_ref;
        std::string determinism_profile;
        std::uint64_t max_latency_sec{0};
        std::uint64_t reward_budget{0};
        std::uint64_t min_reputation{0};
        std::string status{"open"};
        std::string assigned_worker;
        std::string output_ref;
        std::string result_hash;
        std::uint64_t validation_score{0};
    };

    bool submit_job(const std::string& requester_addr,
                    const std::string& job_type,
                    const std::string& input_ref,
                    const std::string& determinism_profile,
                    std::uint64_t max_latency_sec,
                    std::uint64_t reward_budget,
                    std::uint64_t min_reputation,
                    std::string& job_id,
                    std::string& error);

    bool assign_job(const std::string& job_id,
                    const std::string& worker_addr,
                    std::uint64_t collateral_locked,
                    std::string& error);

    bool submit_result(const std::string& job_id,
                       const std::string& worker_addr,
                       const std::string& output_ref,
                       const std::string& result_hash,
                       const std::string& proof_ref,
                       std::string& error);

    bool submit_validation(const std::string& job_id,
                           const std::string& validator_addr,
                           const std::string& verdict,
                           std::uint64_t score,
                           std::string& error);

    bool job_status(const std::string& job_id, std::string& out, std::string& error) const;
    bool worker_status(const std::string& worker_addr, std::string& out, std::string& error) const;

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    struct Assignment {
        std::string worker_addr;
        std::uint64_t collateral_locked{0};
        std::uint64_t assigned_at{0};
    };

    struct Result {
        std::string worker_addr;
        std::string output_ref;
        std::string result_hash;
        std::string proof_ref;
    };

    struct Validation {
        std::string validator_addr;
        std::string verdict;
        std::uint64_t score{0};
    };

    std::unordered_map<std::string, Job> jobs_;
    std::unordered_map<std::string, Assignment> assignments_;
    std::unordered_map<std::string, Result> results_;
    std::unordered_map<std::string, std::unordered_map<std::string, Validation>> validations_by_job_;
};

} // namespace addition
