#include "addition/pouw_compute.hpp"

#include "addition/crypto.hpp"

#include <limits>
#include <sstream>

namespace addition {

bool PoUWComputeEngine::submit_job(const std::string& requester_addr,
                                   const std::string& job_type,
                                   const std::string& input_ref,
                                   const std::string& determinism_profile,
                                   std::uint64_t max_latency_sec,
                                   std::uint64_t reward_budget,
                                   std::uint64_t min_reputation,
                                   std::string& job_id,
                                   std::string& error) {
    job_id.clear();
    if (requester_addr.empty() || job_type.empty() || input_ref.empty()) {
        error = "invalid compute job params";
        return false;
    }
    if (max_latency_sec == 0 || reward_budget == 0) {
        error = "max_latency_sec and reward_budget must be > 0";
        return false;
    }

    const auto seed = requester_addr + "|" + job_type + "|" + input_ref + "|" + std::to_string(jobs_.size());
    job_id = to_hex(sha3_512_bytes("compute_job|" + seed));
    if (jobs_.count(job_id) > 0) {
        error = "duplicate job id";
        return false;
    }

    Job j{};
    j.job_id = job_id;
    j.requester_addr = requester_addr;
    j.job_type = job_type;
    j.input_ref = input_ref;
    j.determinism_profile = determinism_profile.empty() ? "strict" : determinism_profile;
    j.max_latency_sec = max_latency_sec;
    j.reward_budget = reward_budget;
    j.min_reputation = min_reputation;
    j.status = "open";
    jobs_[job_id] = std::move(j);
    return true;
}

bool PoUWComputeEngine::assign_job(const std::string& job_id,
                                   const std::string& worker_addr,
                                   std::uint64_t collateral_locked,
                                   std::string& error) {
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        error = "job not found";
        return false;
    }
    if (worker_addr.empty() || collateral_locked == 0) {
        error = "invalid assignment params";
        return false;
    }
    if (it->second.status != "open") {
        error = "job not open";
        return false;
    }

    assignments_[job_id] = Assignment{worker_addr, collateral_locked, static_cast<std::uint64_t>(assignments_.size() + 1)};
    it->second.assigned_worker = worker_addr;
    it->second.status = "assigned";
    return true;
}

bool PoUWComputeEngine::submit_result(const std::string& job_id,
                                      const std::string& worker_addr,
                                      const std::string& output_ref,
                                      const std::string& result_hash,
                                      const std::string& proof_ref,
                                      std::string& error) {
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        error = "job not found";
        return false;
    }
    auto ait = assignments_.find(job_id);
    if (ait == assignments_.end()) {
        error = "job not assigned";
        return false;
    }
    if (ait->second.worker_addr != worker_addr) {
        error = "worker mismatch";
        return false;
    }
    if (output_ref.empty() || result_hash.empty()) {
        error = "invalid result params";
        return false;
    }

    results_[job_id] = Result{worker_addr, output_ref, result_hash, proof_ref};
    it->second.output_ref = output_ref;
    it->second.result_hash = result_hash;
    it->second.status = "result_submitted";
    return true;
}

bool PoUWComputeEngine::submit_validation(const std::string& job_id,
                                          const std::string& validator_addr,
                                          const std::string& verdict,
                                          std::uint64_t score,
                                          std::string& error) {
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        error = "job not found";
        return false;
    }
    if (validator_addr.empty() || verdict.empty()) {
        error = "invalid validation params";
        return false;
    }
    if (verdict != "pass" && verdict != "fail") {
        error = "verdict must be pass|fail";
        return false;
    }

    validations_by_job_[job_id][validator_addr] = Validation{validator_addr, verdict, score};

    std::uint64_t pass_count = 0;
    std::uint64_t fail_count = 0;
    std::uint64_t score_sum = 0;
    for (const auto& [_, v] : validations_by_job_[job_id]) {
        if (v.verdict == "pass") ++pass_count;
        else if (v.verdict == "fail") ++fail_count;
        if (score_sum <= (std::numeric_limits<std::uint64_t>::max() - v.score)) {
            score_sum += v.score;
        }
    }

    if (pass_count >= 2 && pass_count > fail_count) {
        it->second.status = "validated";
    } else if (fail_count >= 2 && fail_count >= pass_count) {
        it->second.status = "rejected";
    }
    it->second.validation_score = score_sum;

    return true;
}

bool PoUWComputeEngine::job_status(const std::string& job_id, std::string& out, std::string& error) const {
    out.clear();
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        error = "job not found";
        return false;
    }

    std::uint64_t validators = 0;
    auto vit = validations_by_job_.find(job_id);
    if (vit != validations_by_job_.end()) {
        validators = static_cast<std::uint64_t>(vit->second.size());
    }

    std::ostringstream oss;
    oss << "job_id=" << it->second.job_id
        << " status=" << it->second.status
        << " requester=" << it->second.requester_addr
        << " type=" << it->second.job_type
        << " assigned_worker=" << it->second.assigned_worker
        << " reward_budget=" << it->second.reward_budget
        << " validators=" << validators
        << " validation_score=" << it->second.validation_score;
    out = oss.str();
    return true;
}

bool PoUWComputeEngine::worker_status(const std::string& worker_addr, std::string& out, std::string& error) const {
    out.clear();
    if (worker_addr.empty()) {
        error = "worker empty";
        return false;
    }

    std::uint64_t assigned = 0;
    std::uint64_t submitted = 0;
    std::uint64_t validated = 0;
    std::uint64_t rejected = 0;

    for (const auto& [job_id, a] : assignments_) {
        if (a.worker_addr != worker_addr) continue;
        ++assigned;
        if (results_.count(job_id) > 0) ++submitted;
        auto jit = jobs_.find(job_id);
        if (jit != jobs_.end()) {
            if (jit->second.status == "validated") ++validated;
            else if (jit->second.status == "rejected") ++rejected;
        }
    }

    std::ostringstream oss;
    oss << "worker=" << worker_addr
        << " assigned=" << assigned
        << " submitted=" << submitted
        << " validated=" << validated
        << " rejected=" << rejected;
    out = oss.str();
    return true;
}

std::string PoUWComputeEngine::dump_state() const {
    std::ostringstream oss;
    for (const auto& [id, j] : jobs_) {
        oss << "J|" << j.job_id << '|' << j.requester_addr << '|' << j.job_type << '|' << j.input_ref << '|'
            << j.determinism_profile << '|' << j.max_latency_sec << '|' << j.reward_budget << '|' << j.min_reputation << '|'
            << j.status << '|' << j.assigned_worker << '|' << j.output_ref << '|' << j.result_hash << '|' << j.validation_score << '\n';
    }
    for (const auto& [id, a] : assignments_) {
        oss << "A|" << id << '|' << a.worker_addr << '|' << a.collateral_locked << '|' << a.assigned_at << '\n';
    }
    for (const auto& [id, r] : results_) {
        oss << "R|" << id << '|' << r.worker_addr << '|' << r.output_ref << '|' << r.result_hash << '|' << r.proof_ref << '\n';
    }
    for (const auto& [id, vm] : validations_by_job_) {
        for (const auto& [validator, v] : vm) {
            (void)validator;
            oss << "V|" << id << '|' << v.validator_addr << '|' << v.verdict << '|' << v.score << '\n';
        }
    }
    return oss.str();
}

bool PoUWComputeEngine::load_state(const std::string& state, std::string& error) {
    jobs_.clear();
    assignments_.clear();
    results_.clear();
    validations_by_job_.clear();

    std::istringstream iss(state);
    for (std::string line; std::getline(iss, line);) {
        if (line.empty() || line.size() < 2 || line[1] != '|') {
            continue;
        }
        const char tag = line[0];
        std::istringstream ls(line.substr(2));
        if (tag == 'J') {
            Job j{};
            std::string max_latency_sec, reward_budget, min_reputation, validation_score;
            std::getline(ls, j.job_id, '|');
            std::getline(ls, j.requester_addr, '|');
            std::getline(ls, j.job_type, '|');
            std::getline(ls, j.input_ref, '|');
            std::getline(ls, j.determinism_profile, '|');
            std::getline(ls, max_latency_sec, '|');
            std::getline(ls, reward_budget, '|');
            std::getline(ls, min_reputation, '|');
            std::getline(ls, j.status, '|');
            std::getline(ls, j.assigned_worker, '|');
            std::getline(ls, j.output_ref, '|');
            std::getline(ls, j.result_hash, '|');
            std::getline(ls, validation_score);
            j.max_latency_sec = static_cast<std::uint64_t>(std::stoull(max_latency_sec));
            j.reward_budget = static_cast<std::uint64_t>(std::stoull(reward_budget));
            j.min_reputation = static_cast<std::uint64_t>(std::stoull(min_reputation));
            j.validation_score = static_cast<std::uint64_t>(std::stoull(validation_score));
            jobs_[j.job_id] = std::move(j);
        } else if (tag == 'A') {
            std::string job_id;
            Assignment a{};
            std::string collateral_locked, assigned_at;
            std::getline(ls, job_id, '|');
            std::getline(ls, a.worker_addr, '|');
            std::getline(ls, collateral_locked, '|');
            std::getline(ls, assigned_at);
            a.collateral_locked = static_cast<std::uint64_t>(std::stoull(collateral_locked));
            a.assigned_at = static_cast<std::uint64_t>(std::stoull(assigned_at));
            assignments_[job_id] = std::move(a);
        } else if (tag == 'R') {
            std::string job_id;
            Result r{};
            std::getline(ls, job_id, '|');
            std::getline(ls, r.worker_addr, '|');
            std::getline(ls, r.output_ref, '|');
            std::getline(ls, r.result_hash, '|');
            std::getline(ls, r.proof_ref);
            results_[job_id] = std::move(r);
        } else if (tag == 'V') {
            std::string job_id;
            Validation v{};
            std::string score;
            std::getline(ls, job_id, '|');
            std::getline(ls, v.validator_addr, '|');
            std::getline(ls, v.verdict, '|');
            std::getline(ls, score);
            v.score = static_cast<std::uint64_t>(std::stoull(score));
            validations_by_job_[job_id][v.validator_addr] = std::move(v);
        }
    }

    return true;
}

} // namespace addition
