#pragma once
// Hybrid PoW/PoS consensus staking (Decred-style tickets).
//
// * stake lock   : a normal signed tx with an output to kStakeAddress. That
//                  output becomes a ticket (id = "txid:index") owned by the
//                  tx signer, bound to the signer's ML-DSA-87 pubkey.
// * maturity     : a ticket is eligible for selection only once
//                  lock_height + stake_maturity <= parent height.
// * selection    : block h-1's hash seeds a deterministic, stake-weighted
//                  draw (without replacement) of stake_votes_per_block tickets.
// * votes        : block h must carry >= stake_vote_quorum approving votes,
//                  signed by the selected tickets, on block h-1's hash
//                  (enforced once the mature pool has stake_min_live_tickets).
// * reward split : stake_reward_bps of (max mintable subsidy + fees) is paid
//                  in the coinbase, split equally among included votes.
// * unstake      : a signed tx spending the ticket outpoint; its outputs stay
//                  frozen for stake_unbond_delay blocks.
// * slashing     : two conflicting signed votes by one ticket at one height
//                  are evidence; the ticket is burned (coins destroyed).
// All of this is validated by Chain::add_block and rebuilt by block replay,
// so it is persisted with the chain (blocks.dat / P2P BLB3 payloads).
#include "addition/block.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace addition {

// Reserved recipient for locked stake. Not a derivable address (addresses are
// pubkey-bound), so nobody can spend it through the normal owner check.
inline constexpr const char* kStakeAddress = "@stake";

struct StakeTicket {
    std::string id;           // "txid:index"
    std::string owner;        // address that locked it
    std::string owner_pubkey; // signs votes
    std::uint64_t amount{0};
    std::uint64_t lock_height{0};
};

// Canonical message a ticket signs. Domain-separated by network id.
std::string stake_vote_message(const std::string& network_id,
                               std::uint64_t voted_height,
                               const std::string& voted_hash,
                               const std::string& ticket_id,
                               bool approve);

// Deterministic stake-weighted selection without replacement. `eligible` may
// be in any order (it is sorted by id internally). Returns ticket ids in draw
// order. Seed is typically the hex hash of the parent block.
std::vector<std::string> select_tickets(std::vector<StakeTicket> eligible,
                                        const std::string& seed,
                                        std::uint32_t count);

// Stakers' pool = (subsidy + fees) * bps / 10000; per-vote share is that pool
// divided by the number of approving votes (remainder goes to the miner).
std::uint64_t stake_reward_pool(std::uint64_t subsidy_plus_fees, std::uint32_t bps);
std::uint64_t stake_reward_per_vote(std::uint64_t subsidy_plus_fees, std::uint32_t bps, std::size_t votes);

// Deprecated: the old in-memory staking ledger (stake/claim minted coins off
// chain, which bypassed the supply cap). Kept only as an inert type so older
// callers compile; it holds no balances. Use Chain staking instead.
class StakingEngine {
public:
    bool stake(const std::string&, std::uint64_t, std::string& error);
    bool unstake(const std::string&, std::uint64_t, std::string& error);
    std::uint64_t staked_of(const std::string&) const { return 0; }
    std::uint64_t total_staked() const { return 0; }
    void distribute_epoch_rewards(std::uint64_t) {}
    void set_reward_cap_bps(std::uint64_t) {}
    std::uint64_t reward_cap_bps() const { return 0; }
    std::uint64_t claimable_of(const std::string&) const { return 0; }
    std::uint64_t claim(const std::string&) { return 0; }
    bool consume_staked_credit(const std::string&, std::uint64_t, std::string& error);
    const std::unordered_map<std::string, std::uint64_t>& stakes_map() const { return empty_; }
    const std::unordered_map<std::string, std::uint64_t>& claimable_map() const { return empty_; }
    void replace_state(const std::unordered_map<std::string, std::uint64_t>&,
                       const std::unordered_map<std::string, std::uint64_t>&,
                       std::uint64_t) {}

private:
    std::unordered_map<std::string, std::uint64_t> empty_;
};

} // namespace addition
