#include "addition/config.hpp"
#include "addition/crypto.hpp"

#include <chrono>
#include <iostream>
#include <string>

int main() {
    // FIPS 202 / NIST SHA3-512 short vectors (OpenSSL EVP_sha3_512 — not a stub).
    const auto empty_hex = addition::to_hex(addition::sha3_512_bytes(std::string{}));
    const auto abc_hex = addition::to_hex(addition::sha3_512_bytes(std::string{"abc"}));
    const std::string kEmpty =
        "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
        "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26";
    const std::string kAbc =
        "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
        "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0";
    if (empty_hex != kEmpty) {
        std::cerr << "test failed: SHA3-512 empty vector mismatch\n";
        return 1;
    }
    if (abc_hex != kAbc) {
        std::cerr << "test failed: SHA3-512 abc vector mismatch\n";
        return 1;
    }

    // Design lock: chain hashing / addresses / privacy opening use SHA3-512.
    // Jeremy's "sha3-256 etc" maps to the SHA3 family already chosen as SHA3-512
    // (no SHA3-256 path in config.hpp / chain.cpp).
    if (addition::kMemoryHardScratchBytes != (1U << 20) || addition::kMemoryHardRounds != 16) {
        std::cerr << "test failed: memory_hard must stay 1 MiB x 16 rounds\n";
        return 1;
    }

    // Network PoW assignment must not be confused.
    if (addition::testnet_chain_config().pow_algorithm != addition::PowAlgorithm::Sha3_512) {
        std::cerr << "test failed: testnet must keep sha3_512 PoW\n";
        return 1;
    }
    if (addition::mainnet_chain_config().pow_algorithm != addition::PowAlgorithm::MemoryHard) {
        std::cerr << "test failed: mainnet must keep memory_hard PoW\n";
        return 1;
    }

    const std::string seed = "addition-memory-hard-selftest";
    const auto t0 = std::chrono::steady_clock::now();
    const auto a = addition::memory_hard_head64(seed);
    const auto t1 = std::chrono::steady_clock::now();
    const auto b = addition::memory_hard_head64(seed);
    const auto plain = addition::hash_head64(addition::to_hex(addition::sha3_512_bytes(seed)));
    constexpr std::uint64_t kExpected = 0x316e8360b8cde37bULL;

    if (a != kExpected || b != kExpected) {
        std::cerr << "test failed: memory_hard vector mismatch a=" << std::hex << a
                  << " b=" << b << " expected=" << kExpected << '\n';
        return 1;
    }
    if (a == plain) {
        std::cerr << "test failed: memory_hard must not equal plain SHA3 head64 (no-op)\n";
        return 1;
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    // A real 1 MiB x 16-round mix is not instantaneous; allow CI hosts some slack.
    if (ms < 1) {
        std::cerr << "test failed: memory_hard finished in <1ms (" << ms
                  << "ms) — likely a stub/no-op\n";
        return 1;
    }

    std::string report;
    if (!addition::crypto_selftest(report)) {
        std::cerr << "test failed: crypto_selftest: " << report << '\n';
        return 1;
    }
    if (report.find("sha3_512=real") == std::string::npos ||
        report.find("memory_hard=real") == std::string::npos) {
        std::cerr << "test failed: selftest must report real sha3_512+memory_hard: " << report
                  << '\n';
        return 1;
    }

    // Different seeds must not collide trivially (stub returning constant).
    if (addition::memory_hard_head64(seed + "|other") == a) {
        std::cerr << "test failed: memory_hard ignores seed (constant stub)\n";
        return 1;
    }

    std::cout << "crypto SHA3-512 + memory_hard tests passed (" << ms << "ms/attempt)\n";
    return 0;
}
