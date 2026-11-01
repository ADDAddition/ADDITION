// AMLS1 PQ session tests: signed ML-DSA-87 key packages, directory discovery,
// ML-KEM-1024 handshake, KEM double ratchet, transport in the PMSG envelope.
// All keys are throwaway, generated in-test.
#include "addition/crypto.hpp"
#include "addition/pm_relay.hpp"
#include "addition/pq_session.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>

using namespace addition;
using namespace addition::pqmsg;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cerr << "FAIL: " << msg << " (" #c ") line " << __LINE__ << "\n"; ++failures; } } while (0)

static std::uint64_t now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

static void deliver(PmRelay& from, const std::string& from_name, PmRelay& to) {
    for (const auto& line : from.undelivered_for(to.key_dir(), 64)) {
        bool dup = false;
        std::string err;
        bool ok = false;
        if (line.rfind("PMSG|", 0) == 0) ok = to.accept_wire(from_name, line.substr(5), dup, err);
        else if (line.rfind("KPKG|", 0) == 0) ok = to.accept_package(from_name, line.substr(5), dup, err);
        else if (line.rfind("PMDEL|", 0) == 0) ok = to.accept_delete(from_name, line.substr(6), dup, err);
        if (ok) from.mark_delivered(to.key_dir(), line);
    }
}

int main() {
    const auto dir = fs::temp_directory_path() / ("pqsess_test_" + std::to_string(::getpid()));
    PmRelay ra((dir / "a").string()), rb((dir / "b").string()), rmid((dir / "mid").string());
    Messenger alice((dir / "a").string(), &ra), bob((dir / "b").string(), &rb);
    std::string err, contact_a, contact_b, fp_a, fp_b;

    // --- key packages ---------------------------------------------------
    CHECK(alice.publish("alice", "-", 30, contact_a, fp_a, err), "alice publish unlisted: " << err);
    CHECK(bob.publish("bob", "Bob.Example", 7, contact_b, fp_b, err), "bob publish listed: " << err);
    KeyPackage kb;
    CHECK(KeyPackage::from_hex(contact_b, kb, err) && kb.verify(now(), err), "bob package verifies");
    CHECK(kb.index == handle_index("bob.example") && fingerprint(kb.identity_pub) == fp_b, "index/fingerprint");
    {   // tampered init key -> ML-DSA-87 signature fails
        KeyPackage t = kb; t.init_pub[10] ^= 1;
        CHECK(!t.verify(now(), err) && err.find("signature") != std::string::npos, "tampered package rejected");
        KeyPackage u = kb; u.identity_pub[5] ^= 1;
        CHECK(!u.verify(now(), err), "swapped identity rejected");
        KeyPackage x = kb; x.expires = x.created + kMaxPackageLifetime + 1;
        CHECK(!x.verify(now(), err), "over-long lifetime rejected");
        CHECK(!kb.verify(kb.expires + 1, err) && err == "key package expired", "expired package rejected");
        // re-signed by a different ML-DSA-87 key (impersonation) -> invalid for that identity
        Bytes pk, sk;
        CHECK(sig_keypair(pk, sk), "keygen");
        KeyPackage y = kb; CHECK(sig_sign(sk, y.tbs(), y.sig), "sign");
        CHECK(!y.verify(now(), err), "foreign signature rejected");
    }

    // --- directory discovery (gossip b -> mid -> a) -----------------------
    deliver(rb, "b", rmid);
    deliver(rmid, "mid", ra);
    KeyPackage found;
    CHECK(alice.lookup("bob.example", found, err), "alice discovers bob by handle: " << err);
    CHECK(found.hex() == kb.hex() && fingerprint(found.identity_pub) == fp_b, "discovered package is bob's signed package");
    CHECK(!alice.lookup("nobody", found, err), "unknown handle");
    {   // squatting: another identity cannot take over a live index
        PmRelay re((dir / "e").string());
        Messenger eve((dir / "e").string(), &re);
        std::string c, f;
        CHECK(eve.publish("eve", "bob.example", 7, c, f, err), "eve local publish");
        bool dup = false;
        CHECK(!rmid.accept_package("e", "v1|" + c.substr(6), dup, err) && err.find("another identity") != std::string::npos,
              "index squat rejected");
        auto bad = c.substr(6); bad[bad.size() - 10] = bad[bad.size() - 10] == '0' ? '1' : '0';
        CHECK(!rmid.accept_package("e", "v1|" + bad, dup, err), "tampered KPKG rejected");
    }

    // --- handshake + ratchet over the PMSG envelope ------------------------
    std::string sid, out;
    CHECK(alice.start("alice", "bob.example", "hello bob (discovered key)", 600, sid, err), "start: " << err);
    deliver(ra, "a", rmid);
    deliver(rmid, "mid", rb);
    std::string rel;
    CHECK(!rmid.read("bob", "x", rel, err), "relay holds no keys");
    CHECK(bob.receive("bob", out, err), "bob receive: " << err);
    CHECK(out.find("received=1") == 0 && out.find("text=hello bob (discovered key)") != std::string::npos &&
          out.find("from=" + fp_a) != std::string::npos, "bob got init, sender identity authenticated: " << out);
    CHECK(bob.send("bob", sid, "hi alice", 600, err), "bob reply: " << err);
    deliver(rb, "b", ra);
    CHECK(alice.receive("alice", out, err) && out.find("text=hi alice") != std::string::npos, "alice got reply: " << out);
    std::string sl;
    CHECK(alice.sessions("alice", sl, err) && sl.find(sid) != std::string::npos, "session listed");
    std::string slb;
    CHECK(bob.sessions("bob", slb, err), "bob sessions");
    auto safety = [](const std::string& s) { auto p = s.find("safety="); return s.substr(p, 39); };
    CHECK(safety(sl) == safety(slb), "safety numbers match");

    // --- frame-level: out of order, replay, tamper, FS / PCS ---------------
    Bytes f1, f2, f3, g;
    std::string s2, fp, text;
    CHECK(alice.send_frame("alice", sid, "m1", f1, err) && alice.send_frame("alice", sid, "m2", f2, err) &&
          alice.send_frame("alice", sid, "m3", f3, err), "alice 3 frames");
    CHECK(bob.open_frame("bob", f3, s2, fp, text, err) && text == "m3", "out of order m3: " << err);
    CHECK(bob.open_frame("bob", f1, s2, fp, text, err) && text == "m1", "late m1 via skipped key");
    CHECK(!bob.open_frame("bob", f1, s2, fp, text, err), "replay rejected");
    Bytes t2 = f2; t2[t2.size() - 3] ^= 1;
    CHECK(!bob.open_frame("bob", t2, s2, fp, text, err) && err == "authentication failed", "tamper rejected");
    CHECK(bob.open_frame("bob", f2, s2, fp, text, err) && text == "m2", "state intact after tamper");

    // Snapshot bob's full state (attacker compromise), then heal with a round trip.
    const auto snap = dir / "snap";
    fs::copy(dir / "b", snap, fs::copy_options::recursive);
    Messenger stolen(snap.string(), nullptr);
    CHECK(bob.send_frame("bob", sid, "b1", g, err), "bob step");
    CHECK(alice.open_frame("alice", g, s2, fp, text, err) && text == "b1", "alice gets b1");
    Bytes a1;
    CHECK(alice.send_frame("alice", sid, "after heal", a1, err), "alice step");
    CHECK(bob.open_frame("bob", a1, s2, fp, text, err) && text == "after heal", "bob gets healed msg");
    CHECK(!stolen.open_frame("bob", a1, s2, fp, text, err), "post-compromise security: stolen state cannot read");
    // Forward secrecy: a message already consumed can't be reopened from current state.
    CHECK(!bob.open_frame("bob", f3, s2, fp, text, err), "forward secrecy: consumed key gone");

    // INIT signature tamper + rotated init key still opens in-flight INIT.
    Bytes init;
    std::string sid2;
    CHECK(alice.start_frame("alice", kb, "second", sid2, init, err), "second init");
    std::string c2, f2s;
    CHECK(bob.publish("bob", "bob.example", 7, c2, f2s, err) && f2s == fp_b, "bob rotates package, same identity");
    Bytes bad = init; bad[init.size() / 2] ^= 1;
    CHECK(!bob.open_frame("bob", bad, s2, fp, text, err), "tampered INIT rejected");
    CHECK(bob.open_frame("bob", init, s2, fp, text, err) && text == "second" && s2 == sid2, "in-flight INIT after rotation");
    CHECK(!bob.open_frame("bob", init, s2, fp, text, err), "INIT replay rejected");
    bool dup = false;
    CHECK(rb.accept_package("x", "v1|" + c2.substr(6), dup, err) && dup, "older/same package not re-stored");

    fs::remove_all(dir);
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "test_pq_session: all passed\n";
    return 0;
}
