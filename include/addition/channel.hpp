#pragma once
// Post-quantum (ML-DSA-87) Lightning-style payment channels over script v1.
// See docs/PAYMENT_CHANNELS.md. Peer messaging is in-process (structs passed
// between Channel objects); no network transport is provided.
#include "addition/block.hpp"
#include "addition/wallet_keys.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace addition {
class Chain;
}

namespace addition::channel {

struct Htlc {
    std::uint64_t id{0};
    bool offered_by_a{true};
    std::uint64_t amount{0};
    std::string payment_hash;  // sha3-512 of preimage (hex, 128)
    std::uint64_t expiry{0};   // absolute height (after())
};

struct State {
    std::uint64_t n{0};
    std::uint64_t bal_a{0};
    std::uint64_t bal_b{0};
    std::vector<Htlc> htlcs;
    std::uint64_t next_htlc_id{0};
};

struct Info {
    std::string addr_a, addr_b;  // A = funder (pays the commitment fee)
    std::uint64_t capacity{0};
    std::string funding_txid;
    std::uint32_t funding_vout{0};
    std::uint64_t to_self_delay{6};
    std::uint64_t fee{1};
};

// --- scripts (deterministic, both sides can rebuild) ---
std::string funding_script(const std::string& a, const std::string& b);
std::string to_local_script(const std::string& holder, const std::string& other, const std::string& rev_hash, std::uint64_t delay);
std::string htlc_script(const std::string& revoker, const std::string& offerer, const std::string& receiver,
                        const std::string& rev_hash, const std::string& payment_hash, std::uint64_t expiry);
// Commitment held by A (holder_a) or B; unsigned (no witness).
Transaction commitment_tx(const Info& info, bool holder_a, const State& s, const std::string& holder_rev_hash);

struct CommitSig { std::uint64_t n; std::string sig; };
struct Revoke { std::uint64_t revoked_n; std::string secret; std::string next_hash; };

class Channel {
public:
    // seed_hex: per-channel secret used to derive revocation secrets (test-generated).
    Channel(WalletKeys local, std::string remote_pubkey, bool local_is_a, Info info, std::string seed_hex);

    // Opening: exchange first two revocation hashes, then initial commitment sigs.
    std::pair<std::string, std::string> initial_hashes() const;  // hash(0), hash(1)
    void set_remote_initial_hashes(const std::string& h0, const std::string& h1);
    void set_funding(const std::string& txid, std::uint32_t vout);

    // Update protocol (lock-step, both parties agree on `next`).
    bool validate_next(const State& next, std::string& err) const;
    CommitSig sign_remote_commitment(const State& next) const;
    bool receive_commit_sig(const State& next, const CommitSig& cs, std::string& err);
    Revoke revoke_and_advance();  // reveals secret for current n, moves to pending state
    bool receive_revoke(const Revoke& r, std::string& err);

    // On-chain transactions.
    Transaction signed_local_commitment() const;           // unilateral close
    std::string sign_coop_close() const;
    Transaction coop_close_tx() const;                     // unsigned
    bool assemble_coop_close(const std::string& remote_sig, Transaction& out, std::string& err) const;
    std::optional<Transaction> penalty_for(const Transaction& broadcast) const;  // justice tx if revoked
    std::optional<Transaction> sweep_to_local(const Transaction& my_commitment) const;
    std::optional<Transaction> sweep_htlc_timeout(const Transaction& commitment, bool commitment_is_mine, std::uint64_t htlc_id) const;
    std::optional<Transaction> sweep_htlc_success(const Transaction& commitment, bool commitment_is_mine, std::uint64_t htlc_id,
                                                  const std::string& preimage) const;

    // Accessors.
    const State& state() const { return cur_; }
    const Info& info() const { return info_; }
    bool is_a() const { return is_a_; }
    const std::string& local_address() const { return local_.address; }
    std::string remote_address() const { return is_a_ ? info_.addr_b : info_.addr_a; }
    std::uint64_t local_balance() const { return is_a_ ? cur_.bal_a : cur_.bal_b; }
    std::uint64_t remote_balance() const { return is_a_ ? cur_.bal_b : cur_.bal_a; }
    std::string rev_secret(std::uint64_t n) const;
    std::string rev_hash(std::uint64_t n) const;
    std::size_t stored_remote_secrets() const { return remote_secrets_.size(); }

private:
    std::optional<std::pair<std::uint32_t, std::string>> find_output(const Transaction& t, const std::string& script) const;
    Transaction sweep(const Transaction& parent, std::uint32_t vout, const std::string& script,
                      const std::vector<std::string>& preimages, std::uint64_t nonce) const;

    WalletKeys local_;
    std::string remote_pub_;
    bool is_a_;
    Info info_;
    std::string seed_;
    State cur_;
    std::optional<State> pending_;
    std::string remote_sig_cur_;               // remote sig on my commitment for cur_
    std::optional<std::string> remote_sig_pending_;
    bool remote_revoked_pending_{false};
    std::map<std::uint64_t, std::string> remote_hashes_;   // n -> remote holder's rev hash
    std::map<std::uint64_t, std::string> remote_secrets_;  // revealed (revoked) remote secrets
    std::map<std::uint64_t, State> history_;               // states (for penalty reconstruction)
};

// High-level helpers driving both endpoints in-process.
bool open_channel(Chain& chain, const WalletKeys& a, const WalletKeys& b, std::uint64_t capacity, std::uint64_t push_to_b,
                  std::unique_ptr<Channel>& out_a, std::unique_ptr<Channel>& out_b, std::vector<Transaction>& funding_txs, std::string& err);
bool update(Channel& proposer, Channel& peer, const State& next, std::string& err);
bool pay(Channel& payer, Channel& payee, std::uint64_t amount, std::string& err);
bool add_htlc(Channel& payer, Channel& payee, std::uint64_t amount, const std::string& payment_hash,
              std::uint64_t expiry, std::uint64_t& id, std::string& err);
bool settle_htlc(Channel& payee, Channel& payer, std::uint64_t id, const std::string& preimage, std::string& err);
bool fail_htlc(Channel& payee, Channel& payer, std::uint64_t id, std::string& err);

struct Hop { Channel* payer; Channel* payee; };
// Multi-hop HTLC payment; `fee_per_hop` paid to each intermediary; expiries decrease by `delta`.
bool route_payment(const std::vector<Hop>& route, std::uint64_t amount, const std::string& preimage,
                   std::uint64_t final_expiry, std::uint64_t delta, std::uint64_t fee_per_hop, std::string& err);

}  // namespace addition::channel
