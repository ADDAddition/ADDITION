// Tests for the EXPERIMENTAL shielded pool. All keys here are throwaway test
// vectors generated in-process; nothing is printed or persisted.
#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/shielded.hpp"
#include "addition/wallet_keys.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace sh = addition::shielded;

static int g_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cerr << "FAIL: " << msg << " (" #c ")\n"; ++g_fail; } } while (0)

static sh::H32 h(std::uint8_t v) { sh::H32 x{}; x.fill(v); return x; }

static sh::H32 fold_path(sh::H32 leaf, std::uint64_t pos, const std::array<sh::H32, sh::kTreeDepth>& sib) {
    for (std::size_t i = 0; i < sh::kTreeDepth; ++i) {
        leaf = ((pos >> i) & 1) ? sh::merkle_node(sib[i], leaf) : sh::merkle_node(leaf, sib[i]);
    }
    return leaf;
}

static void sign_tx(addition::Transaction& tx, const addition::WalletKeys& k) {
    tx.signer = k.address;
    tx.signer_pubkey = k.public_key;
    tx.signature.clear();
    tx.signature = addition::sign_message_hybrid(k.private_key, addition::hash_transaction(tx));
}

static bool mine(addition::Chain& c, std::vector<addition::Transaction> txs, std::string& err) {
    std::string hash;
    return c.mine_and_add_block("miner_t", std::move(txs), 1, hash, err);
}

int main() {
    std::string err;
    // ---- tree --------------------------------------------------------------
    {
        sh::CommitmentTree t;
        const auto empty_root = t.root();
        for (int i = 0; i < 5; ++i) t.append(h(static_cast<std::uint8_t>(i + 1)));
        CHECK(t.root() != empty_root, "root changes on append");
        for (std::uint64_t p = 0; p < 5; ++p) {
            std::array<sh::H32, sh::kTreeDepth> sib{};
            CHECK(t.path(p, sib), "path exists");
            CHECK(fold_path(h(static_cast<std::uint8_t>(p + 1)), p, sib) == t.root(), "merkle path folds to root");
        }
        std::array<sh::H32, sh::kTreeDepth> sib{};
        CHECK(!t.path(5, sib), "no path past end");
    }
    // ---- keys / addresses / note encryption ---------------------------------
    sh::SpendingKey alice, bob;
    CHECK(sh::generate_spending_key(alice, err), "keygen alice " + err);
    CHECK(sh::generate_spending_key(bob, err), "keygen bob " + err);
    {
        sh::Address a2;
        CHECK(sh::Address::decode(alice.address().encode(), a2, err), "addr decode");
        CHECK(a2.owner == alice.address().owner && a2.kem_pk == alice.address().kem_pk, "addr roundtrip");
        CHECK(alice.address().encode().rfind("addz1", 0) == 0, "addr prefix");
        CHECK(!sh::Address::decode("addz1zz", a2, err), "bad addr rejected");

        addition::Transaction tx; tx.fee = 1;
        sh::Bundle b; b.vpub_in = 7;
        sh::EncryptedNote e;
        const auto r = h(9);
        CHECK(sh::encrypt_note(alice.address(), 7, r, "hello alice, private memo", e, err), "encrypt " + err);
        b.enc.push_back(e);
        const auto bind = sh::bind_hash(tx, b);
        const auto rho = sh::output_rho(bind, b.nullifiers, 0);
        b.commitments.push_back(sh::note_commitment(7, alice.address().owner, rho, r));
        auto n = sh::try_decrypt(alice, tx, b, 0);
        CHECK(n && n->value == 7 && n->memo == "hello alice, private memo" && n->rho == rho, "decrypt ok");
        CHECK(!sh::try_decrypt(bob, tx, b, 0), "wrong key cannot decrypt");
        auto bt = b; bt.enc[0].aead[20] ^= 1;
        CHECK(!sh::try_decrypt(alice, tx, bt, 0), "tampered ciphertext rejected");
        auto bc = b; bc.commitments[0][0] ^= 1;
        CHECK(!sh::try_decrypt(alice, tx, bc, 0), "decrypt rechecks commitment");
        CHECK(e.aead.size() == 12 + 8 + 32 + 2 + sh::kMemoSize + 16, "fixed-size ciphertext hides memo length");
        std::string big(sh::kMemoSize + 1, 'x');
        CHECK(!sh::encrypt_note(alice.address(), 1, r, big, e, err), "oversize memo rejected");
        // serialization
        b.nullifiers.push_back(h(3)); b.anchor = h(4); b.proof = {1, 2, 3};
        sh::Bundle b2;
        CHECK(sh::from_tx_field(sh::to_tx_field(b), b2, err), "deserialize " + err);
        CHECK(sh::serialize(b2) == sh::serialize(b), "serialize roundtrip");
        auto ser = sh::serialize(b); ser.push_back(0);
        CHECK(!sh::deserialize(ser, b2, err), "trailing bytes rejected");
        // nullifier determinism / binding
        CHECK(sh::nullifier(alice.nk, rho) != sh::nullifier(bob.nk, rho), "nf depends on key");
        // without a valid proof: fail closed
        CHECK(!sh::verify_bundle_proof(tx, b, err), "garbage proof rejected");
    }

    auto cfg = addition::testnet_chain_config();
    // ---- network gate ------------------------------------------------------
    {
        auto off = cfg; off.shielded_enabled = false;
        addition::Chain c(off);
        addition::Transaction tx; tx.fee = 1;
        sh::Bundle b; b.nullifiers.push_back(h(1)); b.vpub_out = 1;
        tx.shielded = sh::to_tx_field(b);
        CHECK(!c.validate_transaction(tx, err) && err.find("not enabled") != std::string::npos, "disabled network rejects");
    }
    cfg.shielded_enabled = true;

    if (!sh::proof_system_available()) {
        std::cout << "proof system not built: skipping end-to-end STARK tests\n";
        std::cout << (g_fail ? "FAILED\n" : "ok\n");
        return g_fail ? 1 : 0;
    }

    // ---- end-to-end: t->z, z->z (private payment + memo), z->t ------------------
    addition::Chain chain(cfg);
    const auto wk = addition::generate_wallet_keys();
    CHECK(chain.credit_balance(wk.address, 100, "test_seed", err), "seed " + err);

    // 1) shield 60
    addition::Transaction t1;
    CHECK(chain.build_transaction(wk.address, wk.address, 60, 1, 1, t1, err), "build t1 " + err);
    t1.outputs.erase(t1.outputs.begin());  // the 60 goes into the pool instead
    t1.signer = wk.address; t1.signer_pubkey = wk.public_key;
    sh::Bundle b1;
    double prove_s = 0;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(sh::build_bundle(t1, nullptr, {}, chain.shielded_state().tree(),
                           {{alice.address(), 60, "shield"}}, 60, 0, b1, err, &prove_s), "prove t1 " + err);
    (void)t0;
    t1.shielded = sh::to_tx_field(b1);
    sign_tx(t1, wk);
    {
        auto tv0 = std::chrono::steady_clock::now();
        CHECK(chain.validate_transaction(t1, err), "t1 valid " + err);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tv0).count();
        std::cout << "metric shield(0in,1out): prove_s=" << prove_s << " verify_ms=" << ms
                  << " proof_bytes=" << b1.proof.size() << " bundle_bytes=" << sh::serialize(b1).size() << "\n";
    }
    {   // value mismatch: claim 61 into pool while only paying 60
        auto bad = t1; sh::Bundle bb = b1; bb.vpub_in = 61; bad.shielded = sh::to_tx_field(bb); sign_tx(bad, wk);
        CHECK(!chain.validate_transaction(bad, err), "value/journal mismatch rejected");
        auto bad2 = t1; sh::Bundle bp = b1; bp.proof[bp.proof.size() / 2] ^= 1; bad2.shielded = sh::to_tx_field(bp); sign_tx(bad2, wk);
        CHECK(!chain.validate_transaction(bad2, err), "tampered proof rejected");
        auto bad3 = t1; bad3.fee += 1; sign_tx(bad3, wk);
        CHECK(!chain.validate_transaction(bad3, err), "changing transparent fields breaks binding");
    }
    const auto pos_before = chain.shielded_state().tree().size();
    CHECK(mine(chain, {t1}, err), "mine t1 " + err);
    CHECK(chain.balance_of(wk.address) == 39, "transparent balance after shield");

    // Alice scans.
    auto na = sh::try_decrypt(alice, t1, b1, 0);
    CHECK(na && na->value == 60, "alice receives shielded note");
    if (!na) { std::cerr << "FAILED\n"; return 1; }
    na->position = pos_before;
    CHECK(!sh::try_decrypt(bob, t1, b1, 0), "bob cannot see alice's note");

    // 2) alice -> bob 45 privately, 14 change, fee 1. No transparent inputs/signer.
    addition::Transaction t2; t2.fee = 1;
    sh::Bundle b2;
    CHECK(sh::build_bundle(t2, &alice, {*na}, chain.shielded_state().tree(),
                           {{bob.address(), 45, "private message to bob: lunch at noon?"}, {alice.address(), 14, ""}},
                           0, 1, b2, err, &prove_s), "prove t2 " + err);
    t2.shielded = sh::to_tx_field(b2);
    {
        auto tv0 = std::chrono::steady_clock::now();
        CHECK(chain.validate_transaction(t2, err), "t2 valid " + err);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tv0).count();
        std::cout << "metric z2z(1in,2out): prove_s=" << prove_s << " verify_ms=" << ms
                  << " proof_bytes=" << b2.proof.size() << " bundle_bytes=" << sh::serialize(b2).size()
                  << " tx_hex_chars=" << t2.shielded.size() << "\n";
    }
    // wrong spender key cannot build a spend of alice's note
    {
        addition::Transaction tb; tb.fee = 1; sh::Bundle bb;
        CHECK(!sh::build_bundle(tb, &bob, {*na}, chain.shielded_state().tree(), {{bob.address(), 59, ""}}, 0, 1, bb, err),
              "non-owner cannot prove spend");
        sh::Bundle bv;
        CHECK(!sh::build_bundle(tb, &alice, {*na}, chain.shielded_state().tree(), {{bob.address(), 60, ""}}, 0, 1, bv, err),
              "inflation (out > in) cannot be proven");
    }
    const auto pos2 = chain.shielded_state().tree().size();
    CHECK(mine(chain, {t2}, err), "mine t2 " + err);
    CHECK(chain.validate_transaction(t2, err) == false, "double spend (replay) rejected after mining");
    auto nb = sh::try_decrypt(bob, t2, b2, 0);
    CHECK(nb && nb->value == 45 && nb->memo == "private message to bob: lunch at noon?", "bob gets note + memo");
    CHECK(!sh::try_decrypt(alice, t2, b2, 0), "sender cannot re-read output to bob (no ovk)");
    auto nc = sh::try_decrypt(alice, t2, b2, 1);
    CHECK(nc && nc->value == 14, "alice change");
    if (!nb) { std::cerr << "FAILED\n"; return 1; }
    nb->position = pos2;

    // 3) bob unshields 40 to a transparent address, 4 stays shielded, fee 1
    addition::Transaction t3; t3.fee = 1; t3.outputs.push_back({"bob_transparent", 40});
    sh::Bundle b3;
    CHECK(sh::build_bundle(t3, &bob, {*nb}, chain.shielded_state().tree(), {{bob.address(), 4, ""}}, 0, 41, b3, err, &prove_s),
          "prove t3 " + err);
    t3.shielded = sh::to_tx_field(b3);
    CHECK(chain.validate_transaction(t3, err), "t3 valid " + err);
    // same-block double spend: two txs with the same nullifier
    {
        addition::Transaction t3b; t3b.fee = 1; t3b.outputs.push_back({"bob_transparent", 44});
        sh::Bundle b3b;
        CHECK(sh::build_bundle(t3b, &bob, {*nb}, chain.shielded_state().tree(), {}, 0, 45, b3b, err), "prove t3b " + err);
        t3b.shielded = sh::to_tx_field(b3b);
        CHECK(!mine(chain, {t3, t3b}, err), "in-block double spend rejected");
    }
    CHECK(mine(chain, {t3}, err), "mine t3 " + err);
    CHECK(chain.balance_of("bob_transparent") == 40, "unshielded balance");
    CHECK(chain.shielded_state().nullifier_count() == 2, "two nullifiers spent");

    std::cout << (g_fail ? "FAILED\n" : "ok\n");
    return g_fail ? 1 : 0;
}
