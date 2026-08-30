#pragma once

#include "addition/chain.hpp"
#include "addition/consensus_engine.hpp"
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
    std::unordered_map<std::string, std::unordered_set<std::string>> seen_nonces_;
    std::unordered_map<std::string, std::string> peer_pubkeys_;
    std::unordered_map<std::string, std::string> pinned_peer_pubkeys_;
    std::unordered_set<std::string> seen_rotation_ids_;
    std::unordered_set<std::string> seen_vote_signatures_;
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
