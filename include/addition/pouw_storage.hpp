#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace addition {

class PoUWStorageEngine {
public:
    struct Deal {
        std::string deal_id;
        std::string client_addr;
        std::string content_root;
        std::uint64_t chunk_count{0};
        std::uint64_t replication_factor{0};
        std::uint64_t start_height{0};
        std::uint64_t end_height{0};
        std::uint64_t price_per_epoch{0};
        std::string status{"active"};
    };

    bool create_deal(const std::string& client_addr,
                     const std::string& content_root,
                     std::uint64_t chunk_count,
                     std::uint64_t replication_factor,
                     std::uint64_t start_height,
                     std::uint64_t end_height,
                     std::uint64_t price_per_epoch,
                     std::string& deal_id,
                     std::string& error);

    bool register_commitment(const std::string& deal_id,
                             const std::string& worker_addr,
                             const std::string& sealed_commitment,
                             std::uint64_t collateral,
                             std::string& error);

    bool issue_challenge(const std::string& deal_id,
                         const std::string& worker_addr,
                         std::uint64_t height,
                         std::string& challenge_id,
                         std::string& error);

    bool submit_proof(const std::string& challenge_id,
                      const std::string& worker_addr,
                      const std::string& proof_blob_hash,
                      std::string& verdict,
                      std::string& error);

    bool deal_status(const std::string& deal_id, std::string& out, std::string& error) const;
    bool worker_status(const std::string& worker_addr, std::string& out, std::string& error) const;

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    struct Commitment {
        std::string worker_addr;
        std::string sealed_commitment;
        std::uint64_t collateral{0};
        std::uint64_t proofs_pass{0};
        std::uint64_t proofs_fail{0};
    };

    struct Challenge {
        std::string challenge_id;
        std::string deal_id;
        std::string worker_addr;
        std::uint64_t random_seed{0};
        std::uint64_t chunk_index{0};
        std::uint64_t due_height{0};
        std::string verdict{"pending"};
    };

    std::unordered_map<std::string, Deal> deals_;
    std::unordered_map<std::string, std::vector<Commitment>> commitments_by_deal_;
    std::unordered_map<std::string, Challenge> challenges_;
};

} // namespace addition
