// Payment channel + script v1 tests. All keys are generated in-test (test vectors only).
#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/channel.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/script.hpp"
#include "addition/wallet_keys.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace addition;
namespace ch = addition::channel;

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cerr << "FAIL: " << msg << " (" #c ")\n"; ++failures; } } while (0)

static bool mine(Chain& c, std::vector<Transaction> txs, std::string& err) {
    std::string h;
    return c.mine_and_add_block("miner_reward_sink", std::move(txs), 1, h, err);
}
static void mine_empty(Chain& c, int k) {
    std::string e;
    for (int i = 0; i < k; ++i) if (!mine(c, {}, e)) { std::cerr << "mine failed: " << e << "\n"; ++failures; return; }
}
static bool accepted(Chain& c, const Transaction& t, std::string& err) {
    if (!c.validate_transaction(t, err, true)) return false;
    return mine(c, {t}, err);
}

static void test_script_unit() {
    script::Node n;
    std::string e;
    CHECK(!script::parse("and(pk(a),pk(b)", n, e), "unterminated");
    CHECK(!script::parse("sha3(abc)", n, e), "short hash");
    CHECK(!script::parse("after(007)", n, e), "leading zero");
    CHECK(!script::parse("foo(x)", n, e), "unknown op");
    CHECK(script::parse("or(pk(x),after(5))", n, e), "valid parse");
    std::string deep = "pk(x)";
    for (int i = 0; i < 20; ++i) deep = "and(" + deep + ",pk(x))";
    CHECK(!script::parse(deep, n, e), "depth limit");

    // Witness excluded from txid; merkle root commits to it.
    Transaction t;
    t.inputs.push_back({"aa", 0, ""});
    t.outputs.push_back({"x", 5});
    const auto id1 = hash_transaction(t);
    const auto m1 = compute_merkle_root({t});
    t.inputs[0].witness = "pk(x);0;0;";
    CHECK(hash_transaction(t) == id1, "witness must not change txid");
    CHECK(compute_merkle_root({t}) != m1, "merkle must commit to witness");

    // Signature / preimage evaluation.
    const auto k = generate_wallet_keys();
    const auto k2 = generate_wallet_keys();
    std::string pre;
    random_hex(32, pre, e);
    const auto s = "and(pk(" + k.address + "),sha3(" + script::hash_preimage_hex(pre) + "))";
    script::SpendContext ctx{script::sighash(t), 10, 1};
    const auto sig = script::sign(k.private_key, t);
    CHECK(script::verify_input(script::script_address(s), script::encode_witness({s, {{k.public_key, sig}}, {pre}}), ctx, e), "valid spend: " + e);
    CHECK(!script::verify_input(script::script_address(s), script::encode_witness({s, {{k.public_key, sig}}, {}}), ctx, e), "missing preimage");
    CHECK(!script::verify_input(script::script_address(s), script::encode_witness({s, {{k2.public_key, script::sign(k2.private_key, t)}}, {pre}}), ctx, e), "wrong key");
    CHECK(!script::verify_input(script::script_address(s),
                                script::encode_witness({s, {{k.public_key, sig}, {k2.public_key, script::sign(k2.private_key, t)}}, {pre}}), ctx, e),
          "unused extra sig rejected");
    Transaction t2 = t;
    t2.outputs[0].amount = 6;
    CHECK(!script::verify_input(script::script_address(s), script::encode_witness({s, {{k.public_key, sig}}, {pre}}), {script::sighash(t2), 10, 1}, e),
          "sig bound to tx");
    const auto older = "and(pk(" + k.address + "),older(5))";
    const auto wo = script::encode_witness({older, {{k.public_key, sig}}, {}});
    CHECK(!script::verify_input(script::script_address(older), wo, {ctx.sighash, 5, 1}, e), "older too early");
    CHECK(script::verify_input(script::script_address(older), wo, {ctx.sighash, 6, 1}, e), "older ok");
}

struct Env {
    Chain chain{testnet_chain_config()};
    WalletKeys a = generate_wallet_keys(), b = generate_wallet_keys(), c = generate_wallet_keys();
    Env() {
        std::string e;
        chain.credit_balance(a.address, 100000, "test", e);
        chain.credit_balance(b.address, 100000, "test", e);
        mine_empty(chain, 1);
    }
};

static bool open(Env& env, const WalletKeys& x, const WalletKeys& y, std::uint64_t cap, std::unique_ptr<ch::Channel>& cx,
                 std::unique_ptr<ch::Channel>& cy) {
    std::vector<Transaction> f;
    std::string e;
    if (!ch::open_channel(env.chain, x, y, cap, 0, cx, cy, f, e)) { std::cerr << "open: " << e << "\n"; return false; }
    if (!accepted(env.chain, f[0], e)) { std::cerr << "funding: " << e << "\n"; return false; }
    return true;
}

static void test_coop_and_throughput() {
    Env env;
    std::unique_ptr<ch::Channel> A, B;
    CHECK(open(env, env.a, env.b, 10000, A, B), "open");
    const auto a0 = env.chain.balance_of(env.a.address), b0 = env.chain.balance_of(env.b.address);
    std::string e;
    const int N = 1000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        bool ok = (i % 4 == 3) ? ch::pay(*B, *A, 1, e) : ch::pay(*A, *B, 3, e);
        if (!ok) { CHECK(false, "pay " + std::to_string(i) + ": " + e); break; }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "METRIC off-chain payments: " << N << " in " << secs << " s = " << (N / secs) << " payments/s (single thread, 2 sign + 2 verify each)\n";
    const std::uint64_t exp_b = 750 * 3 - 250;
    CHECK(A->remote_balance() == exp_b && B->local_balance() == exp_b, "balances agree");
    CHECK(A->stored_remote_secrets() == static_cast<std::size_t>(N), "revocations stored");
    Transaction close;
    CHECK(A->assemble_coop_close(B->sign_coop_close(), close, e), "assemble close: " + e);
    std::cout << "METRIC coop close tx bytes(witness): " << close.inputs[0].witness.size() << "\n";
    CHECK(accepted(env.chain, close, e), "close mined: " + e);
    CHECK(env.chain.balance_of(env.b.address) == b0 + exp_b, "B got paid on chain");
    CHECK(env.chain.balance_of(env.a.address) == a0 + (10000 - exp_b - 1), "A refund minus fee");
    // Funding spent: old commitment can no longer confirm.
    CHECK(!env.chain.validate_transaction(A->signed_local_commitment(), e, true), "commitment after close rejected");
}

static void test_cheat_penalized() {
    Env env;
    std::unique_ptr<ch::Channel> A, B;
    CHECK(open(env, env.a, env.b, 10000, A, B), "open");
    std::string e;
    CHECK(ch::pay(*A, *B, 1000, e), "pay1");
    const auto old = A->signed_local_commitment();  // A holds 9000
    std::uint64_t hid = 0;
    CHECK(ch::add_htlc(*A, *B, 500, script::hash_preimage_hex(std::string(64, 'a')), env.chain.height() + 50, hid, e), "htlc");
    CHECK(ch::pay(*A, *B, 7000, e), "pay2");
    const auto old_htlc = A->signed_local_commitment();
    CHECK(ch::pay(*A, *B, 1000, e), "pay3");
    // A cheats with the state holding an HTLC.
    CHECK(accepted(env.chain, old_htlc, e), "cheating commitment confirms: " + e);
    auto premature = A->sweep_to_local(old_htlc);
    CHECK(premature && !env.chain.validate_transaction(*premature, e, true), "A cannot sweep before delay");
    const auto bb = env.chain.balance_of(env.b.address);
    auto justice = B->penalty_for(old_htlc);
    CHECK(justice.has_value(), "penalty built");
    CHECK(!A->penalty_for(old_htlc).has_value(), "A has no penalty against own tx");
    if (justice) {
        CHECK(justice->inputs.size() == 2, "penalty sweeps to_local + htlc");
        CHECK(justice->outputs[0].amount == 1998, "justice amount");
        CHECK(accepted(env.chain, *justice, e), "penalty mined: " + e);
        CHECK(env.chain.balance_of(env.b.address) == bb + 1998, "B sweeps A's to_local+htlc (2000 minus fees)");
    }
    mine_empty(env.chain, 8);
    CHECK(premature && !env.chain.validate_transaction(*premature, e, true), "A sweep after penalty rejected");
    (void)old;
}

static void test_cheat_old_simple() {
    Env env;
    std::unique_ptr<ch::Channel> A, B;
    CHECK(open(env, env.a, env.b, 5000, A, B), "open");
    std::string e;
    const auto s0 = A->signed_local_commitment();
    for (int i = 0; i < 10; ++i) CHECK(ch::pay(*A, *B, 100, e), "pay");
    CHECK(accepted(env.chain, s0, e), "state-0 commitment confirms");
    auto j = B->penalty_for(s0);
    CHECK(j && accepted(env.chain, *j, e), "penalty for state 0: " + e);
}

static void test_timeout_refund_and_success() {
    Env env;
    std::unique_ptr<ch::Channel> A, B;
    CHECK(open(env, env.a, env.b, 10000, A, B), "open");
    std::string e, pre;
    random_hex(32, pre, e);
    const auto ph = script::hash_preimage_hex(pre);
    const std::uint64_t T = env.chain.height() + 10;
    std::uint64_t h1 = 0, h2 = 0;
    CHECK(ch::add_htlc(*A, *B, 700, ph, T, h1, e), "htlc1");
    CHECK(ch::add_htlc(*A, *B, 300, ph, T, h2, e), "htlc2");
    // B goes offline; A force-closes.
    const auto commit = A->signed_local_commitment();
    CHECK(accepted(env.chain, commit, e), "unilateral close: " + e);
    auto refund = A->sweep_htlc_timeout(commit, true, h1);
    CHECK(refund.has_value(), "refund built");
    CHECK(refund && !env.chain.validate_transaction(*refund, e, true), "refund before T rejected");
    // B (if it learns the preimage before T) can claim htlc2 from A's commitment.
    auto claim = B->sweep_htlc_success(commit, false, h2, pre);
    CHECK(claim && accepted(env.chain, *claim, e), "B claims htlc2 with preimage: " + e);
    auto bad = B->sweep_htlc_success(commit, false, h1, std::string(64, 'b'));
    CHECK(bad && !env.chain.validate_transaction(*bad, e, true), "wrong preimage rejected");
    while (env.chain.height() + 1 < T) mine_empty(env.chain, 1);
    const auto ab = env.chain.balance_of(env.a.address);
    CHECK(refund && accepted(env.chain, *refund, e), "timeout refund after T: " + e);
    CHECK(env.chain.balance_of(env.a.address) == ab + 699, "A refunded");
    auto tl = A->sweep_to_local(commit);
    CHECK(tl && accepted(env.chain, *tl, e), "to_local after CSV delay: " + e);
}

static void test_multihop() {
    Env env;
    std::unique_ptr<ch::Channel> AB, BA, BC, CB;
    CHECK(open(env, env.a, env.b, 10000, AB, BA), "open A-B");
    CHECK(open(env, env.b, env.c, 10000, BC, CB), "open B-C");
    std::string e, pre;
    const int N = 50;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        random_hex(32, pre, e);  // invoice preimage chosen by C
        if (!ch::route_payment({{AB.get(), BA.get()}, {BC.get(), CB.get()}}, 10, pre, env.chain.height() + 20, 6, 1, e)) {
            CHECK(false, "route " + e);
            break;
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "METRIC 2-hop HTLC payments: " << N << " in " << secs << " s = " << (N / secs) << " payments/s\n";
    CHECK(CB->local_balance() == 500, "C received 500");
    CHECK(BA->local_balance() == 550 && BC->local_balance() == 10000 - 500, "B earned fees");
    CHECK(AB->local_balance() == 10000 - 550 && AB->state().htlcs.empty() && BC->state().htlcs.empty(), "no stuck htlcs");
    // Bad preimage at settle fails and does not move funds.
    std::uint64_t id = 0;
    CHECK(ch::add_htlc(*AB, *BA, 5, script::hash_preimage_hex(std::string(64, 'c')), env.chain.height() + 20, id, e), "add");
    CHECK(!ch::settle_htlc(*BA, *AB, id, std::string(64, 'd'), e), "bad preimage refused");
    CHECK(ch::fail_htlc(*BA, *AB, id, e) && AB->local_balance() == 10000 - 550, "fail returns funds");
    // Close both cooperatively on chain.
    Transaction c1, c2;
    CHECK(AB->assemble_coop_close(BA->sign_coop_close(), c1, e) && accepted(env.chain, c1, e), "close AB");
    CHECK(BC->assemble_coop_close(CB->sign_coop_close(), c2, e) && accepted(env.chain, c2, e), "close BC");
    CHECK(env.chain.balance_of(env.c.address) == 500, "C on-chain");
}

static void test_consensus_guards() {
    Env env;
    std::unique_ptr<ch::Channel> A, B;
    CHECK(open(env, env.a, env.b, 1000, A, B), "open");
    std::string e;
    // Single-signature spend of 2-of-2 funding must fail.
    Transaction t = A->coop_close_tx();
    script::Witness w{ch::funding_script(env.a.address, env.b.address), {{env.a.public_key, script::sign(env.a.private_key, t)}}, {}};
    t.inputs[0].witness = script::encode_witness(w);
    CHECK(!env.chain.validate_transaction(t, e, true), "1-of-2 rejected");
    // Witness on a legacy signer tx must fail.
    Transaction l;
    CHECK(env.chain.build_transaction(env.a.address, env.b.address, 5, 1, env.chain.next_nonce(env.a.address), l, e), "build");
    l.inputs[0].witness = "x";
    l.signer = env.a.address;
    l.signer_pubkey = env.a.public_key;
    l.signature = sign_message_hybrid(env.a.private_key, hash_transaction(l));
    CHECK(!env.chain.validate_transaction(l, e, true), "legacy+witness rejected");
    // Tampering with an output after signing invalidates the 2-of-2 commitment.
    auto c = A->signed_local_commitment();
    c.outputs[0].amount -= 1;
    c.fee += 1;
    CHECK(!env.chain.validate_transaction(c, e, true), "tampered commitment rejected");
    // Script tx with a block: mined via add_block path (check_signature=false) still verifies scripts.
    std::string h;
    CHECK(!env.chain.mine_and_add_block("m", {t}, 1, h, e), "block with invalid script rejected");
}


// Cross-node update rules (receiver-side classification) and REV-round recovery.
static void test_cross_node_rules_and_recovery() {
    Env env;
    std::unique_ptr<ch::Channel> A, B;
    CHECK(open(env, env.a, env.b, 10000, A, B), "open");
    std::string e;
    const auto h = env.chain.height();
    // Plain payment A->B: B accepts, A (as receiver of such a proposal) rejects.
    ch::State p = A->state(); ++p.n; p.bal_a -= 100; p.bal_b += 100;
    CHECK(ch::classify_peer_update(*B, p, "", h).kind == ch::PeerUpdate::Pay, "B accepts incoming pay");
    CHECK(ch::classify_peer_update(*A, p, "", h).kind == ch::PeerUpdate::Reject, "A rejects outgoing pay proposed by B");
    // HTLC add offered (and funded) by A.
    std::string pre;
    random_hex(32, pre, e);
    const auto ph = script::hash_preimage_hex(pre);
    ch::State add = A->state(); ++add.n; add.bal_a -= 500;
    add.htlcs.push_back(ch::Htlc{add.next_htlc_id++, true, 500, ph, h + 40});
    CHECK(ch::classify_peer_update(*B, add, "", h).kind == ch::PeerUpdate::AddHtlc, "B accepts htlc offered by A");
    CHECK(ch::classify_peer_update(*A, add, "", h).kind == ch::PeerUpdate::Reject, "A rejects htlc 'offered by A' from B");
    ch::State close_exp = add; close_exp.htlcs[0].expiry = h + 1;
    CHECK(ch::classify_peer_update(*B, close_exp, "", h).kind == ch::PeerUpdate::Reject, "expiry too close rejected");
    ch::State unfunded = add; unfunded.bal_a += 500; unfunded.bal_b -= 500;
    CHECK(ch::classify_peer_update(*B, unfunded, "", h).kind == ch::PeerUpdate::Reject, "htlc funded by receiver rejected");
    CHECK(ch::update(*A, *B, add, e), "apply add: " + e);
    // Removal proposed by B (the HTLC receiver).
    ch::State settle = A->state(); ++settle.n; settle.bal_b += 500; settle.htlcs.clear();
    ch::State fail = A->state(); ++fail.n; fail.bal_a += 500; fail.htlcs.clear();
    CHECK(ch::classify_peer_update(*A, settle, pre, h).kind == ch::PeerUpdate::SettleHtlc, "A accepts settle with preimage");
    CHECK(ch::classify_peer_update(*A, settle, std::string(64, 'f'), h).kind == ch::PeerUpdate::Reject, "settle without preimage rejected");
    CHECK(ch::classify_peer_update(*A, fail, "", h).kind == ch::PeerUpdate::FailHtlc, "A accepts fail (refund)");
    CHECK(ch::classify_peer_update(*B, fail, "", h).kind == ch::PeerUpdate::Reject, "offerer may not remove its own htlc off-chain");
    CHECK(ch::update(*B, *A, settle, e), "apply settle: " + e);

    // REV reply lost: A revoked, B advanced; A must not start a new update and
    // can re-send its REV (B answers idempotently).
    ch::State x = A->state(); ++x.n; x.bal_a -= 10; x.bal_b += 10;
    CHECK(B->receive_commit_sig(x, A->sign_remote_commitment(x), e), "B sig: " + e);
    CHECK(A->receive_commit_sig(x, B->sign_remote_commitment(x), e), "A sig: " + e);
    const auto revA = A->revoke_and_advance();
    CHECK(B->receive_revoke(revA, e), "B gets REV");
    const auto revB = B->revoke_and_advance();  // reply lost in transit
    CHECK(A->awaiting_remote_revoke(), "A awaits remote revocation");
    ch::State y = A->state(); ++y.n; y.bal_a -= 1; y.bal_b += 1;
    CHECK(!A->validate_next(y, e) && e == "awaiting remote revocation", "no update while awaiting revocation");
    const auto again = A->last_revoke();
    CHECK(again.revoked_n == revA.revoked_n && again.secret == revA.secret && again.next_hash == revA.next_hash, "REV re-derivable");
    CHECK(!B->has_pending() && B->has_remote_secret(again.revoked_n, again.secret), "B recognises duplicate REV");
    const auto replay = B->last_revoke();
    CHECK(replay.secret == revB.secret && replay.next_hash == revB.next_hash, "B re-sends same revocation");
    CHECK(A->receive_revoke(replay, e), "A recovers: " + e);
    CHECK(!A->awaiting_remote_revoke(), "A no longer awaiting");
    CHECK(ch::pay(*A, *B, 5, e), "channel usable after recovery: " + e);

    // Rollback of a never-revoked pending state keeps penalty safety: the holder
    // of the stale signed state is punishable once that number is revoked.
    ch::State stale = A->state(); ++stale.n; stale.bal_a += 0; stale.bal_b -= 0;
    stale.bal_a -= 3000; stale.bal_b += 3000;  // favourable to B
    CHECK(B->receive_commit_sig(stale, A->sign_remote_commitment(stale), e), "B holds A's sig on stale: " + e);
    auto copy = ch::Channel::deserialize(B->serialize(), env.b, e);  // what a cheating B could keep
    CHECK(copy != nullptr, "copy");
    std::optional<Transaction> stale_tx;
    if (copy) {
        // advance the copy without revealing anything to A to get B's fully signed stale commitment
        copy->revoke_and_advance();
        stale_tx = copy->signed_local_commitment();
    }
    B->drop_pending();                    // rollback on B (ABORT)
    A->note_signed_abandoned(stale);      // A remembers what it signed
    CHECK(!B->has_pending() && B->abandoned_count() == 1, "B rolled back");
    CHECK(ch::pay(*A, *B, 7, e), "new state with same number: " + e);
    CHECK(stale_tx && !A->penalty_for(*stale_tx).has_value(), "not yet revoked");
    CHECK(ch::pay(*A, *B, 7, e), "next update revokes that number: " + e);
    if (stale_tx) {
        CHECK(accepted(env.chain, *stale_tx, e), "stale commitment confirms: " + e);
        auto j = A->penalty_for(*stale_tx);
        CHECK(j.has_value(), "penalty for rolled-back state");
        if (j) CHECK(accepted(env.chain, *j, e), "penalty mined: " + e);
    }
    // persistence keeps abandoned states
    auto re = ch::Channel::deserialize(A->serialize(), env.a, e);
    CHECK(re && re->abandoned_count() == 1, "abandoned persisted");
}

int main() {
    test_script_unit();
    test_coop_and_throughput();
    test_cheat_penalized();
    test_cheat_old_simple();
    test_timeout_refund_and_success();
    test_multihop();
    test_consensus_guards();
    test_cross_node_rules_and_recovery();
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "test_channels: all passed\n";
    return 0;
}
