// Unit tests: on-chain hybrid PoW/PoS staking (tickets, selection, votes,
// quorum, coinbase split, slashing, unbonding) + vote relay + BLB3 payloads.
// Throwaway keys generated in-process only; nothing is printed or persisted.
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/consensus_rules.hpp"
#include "addition/crypto.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/p2p.hpp"
#include "addition/wallet_keys.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void sign(addition::Transaction& tx, const addition::WalletKeys& k) {
    tx.signer = k.address;
    tx.signer_pubkey = k.public_key;
    tx.signature.clear();
    tx.signature = addition::sign_message_hybrid(k.private_key, addition::hash_transaction(tx));
}

addition::StakeVote make_vote(const addition::Chain& c, const std::string& id, const addition::WalletKeys& k,
                              bool approve = true) {
    addition::StakeVote v{};
    v.ticket_id = id;
    v.voted_height = c.height();
    v.voted_hash = addition::hash_block_header(c.block_at(c.height())->header);
    v.approve = approve;
    v.signature = addition::sign_message_hybrid(k.private_key,
                                                c.stake_vote_message_for(v.voted_height, v.voted_hash, id, approve));
    return v;
}

const addition::WalletKeys* owner_keys(const addition::Chain& c, const std::string& id,
                                       const std::vector<addition::WalletKeys>& ks) {
    const auto t = c.ticket(id);
    for (const auto& k : ks) {
        if (t && t->owner == k.address) return &k;
    }
    return nullptr;
}
} // namespace

int main() {
    using namespace addition;
    auto cfg = regtest_chain_config();
    cfg.pow_algorithm = PowAlgorithm::Sha3_512;
    cfg.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    cfg.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    cfg.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    cfg.staking_activation_height = 0;
    cfg.stake_min_amount = 10;
    cfg.stake_maturity = 2;
    cfg.stake_unbond_delay = 3;
    cfg.stake_votes_per_block = 3;
    cfg.stake_vote_quorum = 2;
    cfg.stake_min_live_tickets = 3;
    cfg.stake_reward_bps = 3000;

    Chain chain(cfg);
    Mempool mempool;
    PeerNetwork peers;
    ConsensusEngine consensus;
    const auto node_keys = generate_wallet_keys();
    DecentralizedNode node("self", node_keys.public_key, node_keys.private_key, chain, mempool, peers, consensus);

    std::vector<WalletKeys> k{generate_wallet_keys(), generate_wallet_keys(), generate_wallet_keys()};
    std::string err, h;
    for (int i = 0; i < 3; ++i) {
        check(chain.mine_and_add_block(k[0].address, {}, 1, h, err), "premine " + err);
    }
    std::vector<Transaction> txs;
    for (int i = 1; i <= 2; ++i) {
        Transaction t{};
        check(chain.build_transaction(k[0].address, k[i].address, 30, 1, chain.next_nonce(k[0].address), t, err),
              "fund build " + err);
        sign(t, k[0]);
        check(chain.mine_and_add_block(k[0].address, {t}, 1, h, err), "fund mine " + err);
    }
    // Stake below minimum is rejected; then lock 20 per key.
    {
        Transaction t{};
        check(!chain.build_stake_lock(k[1].address, 5, 1, chain.next_nonce(k[1].address), t, err), "min stake");
    }
    txs.clear();
    for (int i = 0; i < 3; ++i) {
        Transaction t{};
        check(chain.build_stake_lock(k[i].address, 20, 1, chain.next_nonce(k[i].address), t, err), "lock " + err);
        sign(t, k[i]);
        txs.push_back(t);
    }
    check(chain.mine_and_add_block(k[0].address, txs, 1, h, err), "lock mine " + err);
    check(chain.tickets().size() == 3 && chain.total_staked() == 60, "3 tickets / 60 staked");
    check(chain.staked_of(k[1].address) == 20 && chain.balance_of(k[1].address) == 9, "k1 staked 20, balance 9");

    // A ticket outpoint cannot be spent by a normal owner-signed send of @stake funds.
    check(chain.eligible_tickets_for_next_block().empty(), "immature tickets not eligible");
    for (std::uint64_t i = 0; i < cfg.stake_maturity; ++i) {
        check(chain.mine_and_add_block(k[0].address, {}, 1, h, err), "maturity mine " + err);
    }
    check(chain.eligible_tickets_for_next_block().size() == 3, "3 eligible after maturity");
    check(chain.stake_votes_required_for_next_block(), "quorum required");
    check(!chain.mine_and_add_block(k[0].address, {}, 1, h, err) &&
              err.find("insufficient stake votes") != std::string::npos,
          "mining without votes rejected: " + err);

    // Vote relay: encode/decode, pool, and filtering.
    const auto sel = chain.selected_tickets_for_next_block();
    check(sel.size() == 3, "3 selected");
    std::vector<StakeVote> votes;
    for (const auto& id : sel) {
        const auto* ok = owner_keys(chain, id, k);
        check(ok != nullptr, "owner keys");
        if (ok) votes.push_back(make_vote(chain, id, *ok));
    }
    for (const auto& v : votes) {
        StakeVote d{};
        check(DecentralizedNode::decode_stake_vote(DecentralizedNode::encode_stake_vote(v), d, err) &&
                  d.ticket_id == v.ticket_id && d.voted_height == v.voted_height && d.voted_hash == v.voted_hash &&
                  d.approve == v.approve && d.signature == v.signature,
              "SVOTE encode/decode roundtrip");
    }
    {
        StakeVote d{};
        check(!DecentralizedNode::decode_stake_vote("a|b|c", d, err), "SVOTE rejects junk");
    }
    {
        auto forged = votes[0];
        forged.signature = make_vote(chain, votes[0].ticket_id, k[0].address == chain.ticket(votes[0].ticket_id)->owner ? k[1] : k[0]).signature;
        check(!chain.verify_stake_vote(forged, err), "vote signed by wrong key rejected");
        check(!node.submit_stake_vote(forged, err), "node rejects forged vote");
    }
    check(node.submit_stake_vote(votes[0], err), "node accepts vote 0 " + err);
    check(!node.submit_stake_vote(votes[0], err) && err.find("duplicate") != std::string::npos, "duplicate vote");
    // Auto-voter casts the remaining selected votes from local keys only.
    for (int i = 0; i < 3; ++i) node.set_auto_vote_key(k[i].address, k[i].public_key, k[i].private_key);
    check(node.refresh_local_votes() == 2, "auto-voter casts the 2 missing votes");
    check(node.usable_stake_votes().size() == 3, "3 usable votes");
    const auto outbound = node.pull_outbound_messages();
    check(std::count_if(outbound.begin(), outbound.end(),
                        [](const std::string& m) { return m.rfind("SVOTE|", 0) == 0; }) == 3,
          "3 SVOTE messages queued for gossip");

    // Miner path: block carries the votes, coinbase pays each voter.
    const auto b1 = chain.balance_of(k[1].address);
    const auto b2 = chain.balance_of(k[2].address);
    const auto pot = max_mintable_subsidy(chain.height() + 1, chain.total_emitted(), cfg.max_supply, cfg.block_reward,
                                          cfg.halving_interval, cfg.subsidy_floor);
    const auto per_vote = stake_reward_per_vote(pot, cfg.stake_reward_bps, 3);
    Miner miner(chain, mempool);
    miner.set_vote_source([&]() { return node.usable_stake_votes(); });
    check(miner.mine_next_block(k[0].address, 100, 1, h, err), "mine with votes " + err);
    const auto voted = chain.block_at(chain.height());
    check(voted && voted->votes.size() == 3, "block carries 3 votes");
    {
        const auto w = stake_extension_weight(*voted);
        const auto per = stake_vote_weight(voted->votes[0]);
        check(per > 4000 && w >= 3 * per && stake_extension_weight(*chain.block_at(1)) == 0,
              "vote weight counted (" + std::to_string(per) + " B/vote)");
    }
    check(chain.balance_of(k[1].address) == b1 + per_vote && chain.balance_of(k[2].address) == b2 + per_vote && per_vote > 0,
          "voters paid per_vote in coinbase");

    // BLV3 (wire v3, reserved extension slot) and legacy BLB3 (v2 peers) roundtrips.
    {
        const auto payload = node.encode_block_payload_for_test(*voted);
        Block d{};
        check(node.decode_block_payload(payload, d, err), "BLV3 decode " + err);
        check(hash_block_header(d.header) == hash_block_header(voted->header) && d.votes.size() == 3 &&
                  d.votes[0].signature == voted->votes[0].signature && d.votes[2].ticket_id == voted->votes[2].ticket_id &&
                  compute_block_commitment(d) == compute_block_commitment(*voted),
              "BLV3 roundtrip preserves header and votes");
        const auto v2 = node.encode_block_payload_v2_for_test(*voted);
        check(v2.rfind("424c4233", 0) == 0 || v2.rfind("424C4233", 0) == 0, "v2 peers get BLB3 magic");
        Block d2{};
        check(node.decode_block_payload(v2, d2, err) && d2.votes.size() == 3, "BLB3 decode " + err);
        const auto plain = node.encode_block_payload_v2_for_test(*chain.block_at(1));
        check(plain.rfind("424c4232", 0) == 0 || plain.rfind("424C4232", 0) == 0, "vote-less block stays BLB2");
        Block nov = *voted;
        nov.votes.clear();
        check(stake_extension_weight(*voted) > 0 &&
                  block_weight(*voted) == block_weight(nov) + stake_extension_weight(*voted),
              "block_weight counts votes");
    }

    // Equivocation: two conflicting votes by one ticket burn it.
    {
        const auto victim = chain.selected_tickets_for_next_block()[0];
        const auto* vk = owner_keys(chain, victim, k);
        SlashEvidence ev{make_vote(chain, victim, *vk, true), make_vote(chain, victim, *vk, false)};
        std::vector<StakeVote> vs;
        for (const auto& id : chain.selected_tickets_for_next_block()) vs.push_back(make_vote(chain, id, *owner_keys(chain, id, k)));
        const auto before = chain.total_staked();
        check(chain.mine_and_add_block_with_votes(k[0].address, {}, vs, {ev}, 1, h, err), "mine slash " + err);
        check(!chain.ticket(victim).has_value(), "slashed ticket removed");
        check(chain.total_slashed() == 20 && chain.total_staked() == before - 20, "slash burns 20");
    }

    // Unstake: outputs frozen for unbond_delay blocks (2 tickets left < min_live: votes optional).
    {
        check(!chain.stake_votes_required_for_next_block(), "below min_live -> votes optional");
        const auto mine_tickets = chain.tickets_of(k[2].address).empty() ? chain.tickets_of(k[1].address)
                                                                          : chain.tickets_of(k[2].address);
        check(!mine_tickets.empty(), "a ticket to unstake");
        const auto& t = mine_tickets.front();
        const auto& ok = t.owner == k[1].address ? k[1] : k[2];
        Transaction u{};
        check(chain.build_stake_unlock(t.id, 1, chain.next_nonce(ok.address), u, err), "unlock build " + err);
        Transaction stolen = u;
        sign(stolen, k[0]);
        check(!chain.validate_transaction(stolen, err), "unlock by non-owner rejected");
        sign(u, ok);
        check(chain.mine_and_add_block(k[0].address, {u}, 1, h, err), "unlock mine " + err);
        const auto op = chain.outpoint_key(hash_transaction(u), 0);
        check(chain.spendable_height_of(op) == chain.height() + cfg.stake_unbond_delay, "unbond freeze height");
        check(chain.staked_of(ok.address) == 0, "staked 0 after unlock");
        Transaction spend{};
        const auto bal = chain.balance_of(ok.address);
        check(!chain.build_transaction(ok.address, k[0].address, bal - 1, 1, chain.next_nonce(ok.address), spend, err),
              "frozen unbonding coins not spendable yet");
        for (std::uint64_t i = 0; i < cfg.stake_unbond_delay; ++i) chain.mine_and_add_block(k[0].address, {}, 1, h, err);
        check(chain.build_transaction(ok.address, k[0].address, bal - 1, 1, chain.next_nonce(ok.address), spend, err),
              "unbonded coins spendable after delay " + err);
    }

    // Mainnet keeps staking dormant until an activation height is chosen.
    check(!Chain(mainnet_chain_config()).staking_active_at(1), "mainnet staking not active");

    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "test_staking ok\n";
    return 0;
}
