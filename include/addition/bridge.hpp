#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace addition {

class BridgeEngine {
public:
    bool register_chain(const std::string& chain, std::string& error);
    bool lock(const std::string& chain,
              const std::string& user,
              std::uint64_t amount,
              std::string& receipt,
              std::string& error);
    bool mint_wrapped(const std::string& chain,
                      const std::string& user,
                      std::uint64_t amount,
                      std::string& error);
    bool mint_wrapped_attested(const std::string& chain,
                               const std::string& user,
                               std::uint64_t amount,
                               const std::string& attestation,
                               std::string& error);
    bool burn_wrapped(const std::string& chain,
                      const std::string& user,
                      std::uint64_t amount,
                      std::string& error);
    bool release(const std::string& chain,
                 const std::string& user,
                 std::uint64_t amount,
                 std::string& error);
    bool release_attested(const std::string& chain,
                          const std::string& user,
                          std::uint64_t amount,
                          const std::string& attestation,
                          std::string& error);

    bool set_attestor_key(const std::string& chain,
                          const std::string& pubkey_hex,
                          std::string& error);
    std::string attestor_key(const std::string& chain) const;

    std::uint64_t wrapped_balance(const std::string& chain, const std::string& user) const;

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    struct ChainState {
        std::uint64_t locked_pool{0};
        std::string attestor_pubkey;
        std::unordered_map<std::string, std::uint64_t> wrapped_balances;
        std::vector<std::string> receipts;
        std::unordered_set<std::string> used_attestations;
    };

    std::unordered_map<std::string, ChainState> chains_;
};

} // namespace addition
