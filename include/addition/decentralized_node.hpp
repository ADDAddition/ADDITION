#pragma once

#include "addition/chain.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/dandelion.hpp"
#include "addition/mempool.hpp"
#include "addition/p2p.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

namespace addition {

class DecentralizedNode {
public:
    DecentralizedNode(std::string self_id,
                      std::string node_public_key,
                      std::string node_private_key,
                      Chain& chain,
                      Mempool& mempool,
                      PeerNetwork& peers,
                      ConsensusEngine& consensus);

    bool submit_transaction(const Transaction& tx, std::string& error);
    bool ingest_block(const Block& block, std::string& error);
    bool ingest_block_payload(const std::string& payload, std::string& error);
    bool get_block_payload(std::uint64_t height, std::string& payload, std::string& error) const;
    bool decode_block_payload(const std::string& payload, Block& block, std::string& error) const;
    bool sync_once(std::string& error);
    // Queue a BLK| announce for the current tip (used after local mine, which
    // bypasses ingest_block). Does not loosen PoW; tip must already be on chain.
    bool announce_tip(std::string& error);
    // Push queued TX|/BLK| (and rotation) gossip to connected peers over P2P.
    bool flush_outbound_gossip(std::size_t& sent, std::string& error);

    std::vector<std::string> pull_outbound_messages();
    bool handle_inbound_message(const std::string& peer, const std::string& message, std::string& error);
    bool handle_inbound_message(const std::string& peer,
                                const std::string& message,
                                std::string& reply,
                                std::string& error);
    std::string handle_p2p_line(const std::string& line);

    // ---- stake vote relay ----
    // Validate a vote on the current tip (selected ticket, valid signature),
    // add it to the vote pool and queue it for gossip ("SVOTE|...").
    bool submit_stake_vote(const StakeVote& vote, std::string& error);
    // Valid, selected, unique votes for the block the miner builds next.
    std::vector<StakeVote> usable_stake_votes() const;
    std::size_t vote_pool_size() const { return vote_pool_.size(); }
    // Automatic equivocation detection. Every validly signed vote seen (gossip
    // or in a block) is remembered per ticket|height; a second, conflicting
    // vote becomes SlashEvidence that the miner includes (ticket is burned).
    bool observe_vote_for_equivocation(const StakeVote& vote);
    std::vector<SlashEvidence> usable_slash_evidence() const;
    std::size_t pending_slash_count() const { return pending_slashes_.size(); }
    // Local auto-voter. Keys live only in this process (loaded from the local
    // wallet store); they are never sent over RPC or P2P.
    void set_auto_vote_key(const std::string& address, const std::string& public_key,
                           const std::string& private_key);
    void clear_auto_vote_key(const std::string& address);
    std::vector<std::string> auto_vote_addresses() const;
    // Sign+gossip votes for every selected ticket owned by an auto-vote key.
    std::size_t refresh_local_votes();
    static std::string encode_stake_vote(const StakeVote& vote);
    static bool decode_stake_vote(const std::string& payload, StakeVote& vote, std::string& error);
    // Test hook: exposes the block payload encoder (BLB2/BLB3) for unit tests.
    std::string encode_block_payload_for_test(const Block& block) const { return encode_block_payload(block); }
    std::string encode_block_payload_v2_for_test(const Block& block) const { return encode_block_payload_v2(block); }
    std::string adapt_for_peer_for_test(const std::string& msg, int proto) const { return adapt_for_peer(msg, proto); }
    std::string encode_tx_payload_for_test(const Transaction& tx) const { return encode_tx_payload(tx); }
    const std::string& self_id() const;
    const std::string& node_public_key() const;
    const std::string& node_private_key() const;
    std::vector<std::pair<std::string, std::string>> peer_pins() const;
    void load_peer_pins(const std::vector<std::pair<std::string, std::string>>& pins);
    bool propose_identity_rotation(const std::string& new_public_key,
                                   const std::string& new_private_key,
                                   std::uint64_t grace_seconds,
                                   std::string& error);
    bool vote_identity_rotation(const std::string& peer_id, std::string& error);
    bool broadcast_identity_rotation_vote(std::string& error);
    bool commit_identity_rotation(std::string& error);
    std::string identity_rotation_status() const;

    // Dandelion++ tx relay (off by default). When on, locally submitted txs
    // and inbound STEM| txs go to a stem relay instead of being broadcast.
    void set_dandelion(bool enabled);
    bool dandelion_enabled() const;
    std::size_t stempool_size() const;
    DandelionRouter& dandelion_router();
    // Our own reachable address (IPv4 or .onion), sent as ADVERTISE| after a
    // successful outbound handshake so peers can learn inbound-only nodes.
    void set_advertised_p2p(const std::string& endpoint);

private:
    std::string self_id_;
    std::string node_public_key_;
    std::string node_private_key_;
    Chain& chain_;
    Mempool& mempool_;
    PeerNetwork& peers_;
    ConsensusEngine& consensus_;

    std::unordered_set<std::string> seen_txids_;
    std::unordered_set<std::string> seen_block_hashes_;
    std::vector<std::string> outbound_;
    std::unordered_map<std::string, bool> handshake_ok_;
    // Negotiated wire protocol per peer (2 = hex TXB2/BLB2, 3 = base64 TXV3/BLV3,
    // 4 = v3 + TXV3 script witnesses).
    // Unknown peers are treated as v2 (always decodable by both).
    std::unordered_map<std::string, int> peer_proto_;
    int peer_proto(const std::string& peer) const;
    // Transcode canonical v3 TX|/BLK|/STEM|/BLKDATA| lines to v2 for legacy peers.
    std::string adapt_for_peer(const std::string& msg, int proto) const;
    std::string encode_tx_payload_v2(const Transaction& tx) const;
    std::string encode_block_payload_v2(const Block& block) const;
    std::unordered_map<std::string, std::unordered_set<std::string>> seen_nonces_;
    std::unordered_map<std::string, std::string> peer_pubkeys_;
    std::unordered_map<std::string, std::string> pinned_peer_pubkeys_;
    std::unordered_set<std::string> seen_rotation_ids_;
    std::unordered_set<std::string> seen_vote_signatures_;
    std::vector<StakeVote> vote_pool_;
    std::unordered_map<std::string, StakeVote> vote_watch_;   // ticket|height -> first vote
    std::vector<SlashEvidence> pending_slashes_;
    std::unordered_set<std::string> slash_reported_;          // ticket ids with evidence
    std::unordered_set<std::string> seen_stake_votes_; // ticket|height|hash|approve
    struct AutoVoteKey {
        std::string public_key;
        std::string private_key;
    };
    std::unordered_map<std::string, AutoVoteKey> auto_vote_keys_;
    void prune_vote_pool();
    bool sync_once_impl(std::string& error);
    std::unordered_map<std::string, std::unordered_set<std::string>> relayed_message_ids_by_peer_;

    struct RateBucket {
        std::deque<std::uint64_t> generic_hits;
        std::deque<std::uint64_t> expensive_hits;
    };
    std::unordered_map<std::string, RateBucket> rate_limits_;

    struct PendingRotation {
        std::string new_public_key;
        std::string new_private_key;
        std::uint64_t effective_after{0};
        std::string proof;
        std::string rotation_id;
        std::unordered_set<std::string> votes;
        std::unordered_set<std::string> vote_signatures;
    };

    std::optional<PendingRotation> pending_rotation_;

    bool dandelion_ = false;
    DandelionRouter dandelion_router_;
    std::unordered_map<std::string, Transaction> stempool_;
    std::vector<std::pair<std::string, std::string>> stem_outbound_;  // relay -> STEM|...
    std::string advertised_p2p_;
    static constexpr std::size_t kMaxStempool = 5000;

    bool fluff_transaction(const Transaction& tx, std::string& error);
    bool stem_transaction(const Transaction& tx, const std::string& source, std::string& error);
    std::vector<std::string> outbound_relay_peers() const;
    std::size_t flush_stem(std::string& last);

    std::string encode_tx_payload(const Transaction& tx) const;
    bool relay_rotation_messages_to_peer(const std::string& peer, std::string& error);
    bool allow_peer_message(const std::string& peer, bool expensive, std::string& error);
    bool decode_tx_payload(const std::string& payload, Transaction& tx, std::string& error) const;
    std::string encode_block_payload(const Block& block) const;
    bool handshake_with_peer(const std::string& peer, std::string& wire_id, std::string& error);
    bool fetch_blocks_from_peer(const std::string& peer, const std::string& wire_id, std::string& error);
    bool ingest_from_public_rpc(const std::string& p2p_peer, std::string& error);
    bool learn_peers_from(const std::string& peer, const std::string& wire_id, std::string& error);
    std::string encode_tx_gossip(const Transaction& tx) const;
    std::string encode_block_announce(const Block& block) const;
    bool decode_tx_gossip(const std::string& payload, Transaction& tx, std::string& error) const;
    bool decode_block_announce(const std::string& payload, Block& block, std::string& error) const;
    bool is_push_gossip_message(const std::string& message) const;
};

} // namespace addition
