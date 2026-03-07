#pragma once

#include "addition/token_engine.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace addition {

class PrivacyPool;

class ContractEngine {
public:
    explicit ContractEngine(TokenEngine* tokens = nullptr, PrivacyPool* privacy = nullptr);
    void bind_token_engine(TokenEngine* tokens);
    void bind_privacy_pool(PrivacyPool* privacy);

    std::string deploy(const std::string& owner, const std::string& code);
    bool call(const std::string& contract_id,
              const std::string& method,
              const std::string& key,
              std::int64_t value,
              std::string& out,
              std::string& error);

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    struct Contract {
        std::string owner;
        std::string code;
        std::unordered_map<std::string, std::int64_t> kv;
    };

    std::unordered_map<std::string, Contract> contracts_;
    TokenEngine* tokens_{nullptr};
    PrivacyPool* privacy_{nullptr};
};

} // namespace addition
