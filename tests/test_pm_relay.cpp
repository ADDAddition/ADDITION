#include "addition/pm_relay.hpp"
#include <cstdlib>
#include <unistd.h>
#define assert(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << "\n"; std::exit(1); } } while (0)
#include <filesystem>
#include <iostream>
#include <sstream>
int main() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("pmrelay_test_" + std::to_string(::getpid()));
    addition::PmRelay a((dir / "a").string()), c((dir / "c").string()), mid;
    std::string pk, mb, err, id;
    assert(c.ensure_key("bob", pk, mb, err));
    assert(a.send(pk, "hello bob", 120, id, err));
    // a -> mid -> c
    auto lines = a.undelivered_for("mid", 32);
    assert(lines.size() == 1);
    bool dup = false;
    assert(mid.accept_wire("a", lines[0].substr(5), dup, err) && !dup);
    assert(mid.accept_wire("a", lines[0].substr(5), dup, err) && dup);
    assert(mid.undelivered_for("a", 32).empty());          // no echo to sender
    auto fwd = mid.undelivered_for("c", 32);
    assert(fwd.size() == 1);
    assert(c.accept_wire("mid", fwd[0].substr(5), dup, err));
    std::string out;
    assert(!mid.read("bob", id, out, err));                // relay cannot read
    assert(c.read("bob", id, out, err) && out == "hello bob");
    // tamper
    auto bad = fwd[0]; bad[bad.size() - 3] = bad[bad.size() - 3] == '0' ? '1' : '0';
    addition::PmRelay d((dir / "c").string());
    assert(d.accept_wire("x", bad.substr(5), dup, err));
    assert(!d.read("bob", id, out, err));
    // delete propagates with valid token only
    assert(!mid.accept_delete("x", id + "|" + std::string(64, '0'), dup, err));
    assert(c.remove("bob", id, err));
    auto del = c.undelivered_for("mid", 32);
    assert(del.size() == 1 && del[0].rfind("PMDEL|", 0) == 0);
    assert(mid.accept_delete("c", del[0].substr(6), dup, err) && !dup);
    assert(mid.accept_wire("a", lines[0].substr(5), dup, err) && dup);  // tombstoned
    assert(mid.status().find("stored=0") != std::string::npos);
    std::string big(5000, 'x');
    assert(!a.send(pk, big, 120, id, err));
    fs::remove_all(dir);
    std::cout << "pm_relay ok\n";
}
