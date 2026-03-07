#include "addition/bridge.hpp"

#include "addition/crypto.hpp"

#include <limits>
#include <sstream>

namespace addition {

bool BridgeEngine::register_chain(const std::string& chain, std::string& error) {
    if (chain.empty()) {
        error = "chain empty";
        return false;
    }
    chains_.try_emplace(chain, ChainState{});
    return true;
}

bool BridgeEngine::set_attestor_key(const std::string& chain,
                                    const std::string& pubkey_hex,
                                    std::string& error) {
    if (chain.empty() || pubkey_hex.empty()) {
        error = "chain/pubkey empty";
        return false;
    }
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }
    it->second.attestor_pubkey = pubkey_hex;
    return true;
}

std::string BridgeEngine::attestor_key(const std::string& chain) const {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        return {};
    }
    return it->second.attestor_pubkey;
}

bool BridgeEngine::lock(const std::string& chain,
                        const std::string& user,
                        std::uint64_t amount,
                        std::string& receipt,
                        std::string& error) {
    if (amount == 0 || user.empty()) {
        error = "invalid lock params";
        return false;
    }
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }

    if (it->second.locked_pool > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        error = "locked pool overflow";
        return false;
    }
    it->second.locked_pool += amount;
    receipt = to_hex(sha3_512_bytes(chain + "|" + user + "|" + std::to_string(amount)));
    it->second.receipts.push_back(receipt);
    return true;
}

bool BridgeEngine::mint_wrapped(const std::string& chain,
                                const std::string& user,
                                std::uint64_t amount,
                                std::string& error) {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }
    if (amount == 0 || user.empty()) {
        error = "invalid mint params";
        return false;
    }

    if (it->second.locked_pool < amount) {
        error = "insufficient locked collateral for mint";
        return false;
    }

    auto& w = it->second.wrapped_balances[user];
    if (w > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        error = "wrapped balance overflow";
        return false;
    }
    w += amount;
    return true;
}

bool BridgeEngine::mint_wrapped_attested(const std::string& chain,
                                         const std::string& user,
                                         std::uint64_t amount,
                                         const std::string& attestation,
                                         std::string& error) {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }
    if (it->second.attestor_pubkey.empty()) {
        error = "attestor key not configured";
        return false;
    }

    const std::string msg = "bridge_mint|" + chain + "|" + user + "|" + std::to_string(amount);
    if (!verify_message_signature_hybrid(it->second.attestor_pubkey, msg, attestation)) {
        error = "invalid attestation signature";
        return false;
    }
    const auto attestation_id = to_hex(sha3_512_bytes("mint_att|" + chain + "|" + user + "|" + std::to_string(amount) + "|" + attestation));
    if (!it->second.used_attestations.insert(attestation_id).second) {
        error = "attestation replay";
        return false;
    }
    if (!mint_wrapped(chain, user, amount, error)) {
        it->second.used_attestations.erase(attestation_id);
        return false;
    }
    return true;
}

bool BridgeEngine::burn_wrapped(const std::string& chain,
                                const std::string& user,
                                std::uint64_t amount,
                                std::string& error) {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }
    auto& b = it->second.wrapped_balances[user];
    if (b < amount || amount == 0) {
        error = "insufficient wrapped balance";
        return false;
    }
    b -= amount;
    return true;
}

bool BridgeEngine::release(const std::string& chain,
                           const std::string& user,
                           std::uint64_t amount,
                           std::string& error) {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }
    if (it->second.locked_pool < amount || amount == 0 || user.empty()) {
        error = "insufficient locked pool";
        return false;
    }

    auto& wrapped = it->second.wrapped_balances[user];
    if (wrapped < amount) {
        error = "insufficient wrapped balance for release";
        return false;
    }
    wrapped -= amount;

    it->second.locked_pool -= amount;
    return true;
}

bool BridgeEngine::release_attested(const std::string& chain,
                                    const std::string& user,
                                    std::uint64_t amount,
                                    const std::string& attestation,
                                    std::string& error) {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        error = "chain not registered";
        return false;
    }
    if (it->second.attestor_pubkey.empty()) {
        error = "attestor key not configured";
        return false;
    }

    const std::string msg = "bridge_release|" + chain + "|" + user + "|" + std::to_string(amount);
    if (!verify_message_signature_hybrid(it->second.attestor_pubkey, msg, attestation)) {
        error = "invalid attestation signature";
        return false;
    }
    const auto attestation_id = to_hex(sha3_512_bytes("release_att|" + chain + "|" + user + "|" + std::to_string(amount) + "|" + attestation));
    if (!it->second.used_attestations.insert(attestation_id).second) {
        error = "attestation replay";
        return false;
    }
    if (!release(chain, user, amount, error)) {
        it->second.used_attestations.erase(attestation_id);
        return false;
    }
    return true;
}

std::uint64_t BridgeEngine::wrapped_balance(const std::string& chain, const std::string& user) const {
    auto it = chains_.find(chain);
    if (it == chains_.end()) {
        return 0;
    }
    auto jt = it->second.wrapped_balances.find(user);
    return jt == it->second.wrapped_balances.end() ? 0ULL : jt->second;
}

std::string BridgeEngine::dump_state() const {
    std::ostringstream oss;
    for (const auto& [chain, st] : chains_) {
        oss << "C|" << chain << '|' << st.locked_pool << '\n';
        if (!st.attestor_pubkey.empty()) {
            oss << "A|" << chain << '|' << st.attestor_pubkey << '\n';
        }
        for (const auto& a : st.used_attestations) {
            oss << "Y|" << chain << '|' << a << '\n';
        }
        for (const auto& [user, amount] : st.wrapped_balances) {
            oss << "W|" << chain << '|' << user << '|' << amount << '\n';
        }
        for (const auto& r : st.receipts) {
            oss << "R|" << chain << '|' << r << '\n';
        }
    }
    return oss.str();
}

bool BridgeEngine::load_state(const std::string& state, std::string& error) {
    chains_.clear();

    std::istringstream iss(state);
    for (std::string line; std::getline(iss, line);) {
        if (line.empty()) {
            continue;
        }

        std::istringstream ls(line);
        std::string tag;
        std::getline(ls, tag, '|');

        if (tag == "C") {
            std::string chain;
            std::string locked;
            std::getline(ls, chain, '|');
            std::getline(ls, locked);
            if (chain.empty()) {
                error = "invalid bridge chain line";
                return false;
            }
            chains_[chain].locked_pool = static_cast<std::uint64_t>(std::stoull(locked));
        } else if (tag == "A") {
            std::string chain;
            std::string key;
            std::getline(ls, chain, '|');
            std::getline(ls, key);
            if (chain.empty() || key.empty()) {
                error = "invalid bridge attestor line";
                return false;
            }
            chains_[chain].attestor_pubkey = key;
        } else if (tag == "W") {
            std::string chain;
            std::string user;
            std::string amount;
            std::getline(ls, chain, '|');
            std::getline(ls, user, '|');
            std::getline(ls, amount);
            if (chain.empty() || user.empty()) {
                error = "invalid bridge wrapped line";
                return false;
            }
            chains_[chain].wrapped_balances[user] = static_cast<std::uint64_t>(std::stoull(amount));
        } else if (tag == "Y") {
            std::string chain;
            std::string aid;
            std::getline(ls, chain, '|');
            std::getline(ls, aid);
            if (chain.empty() || aid.empty()) {
                error = "invalid bridge attestation line";
                return false;
            }
            chains_[chain].used_attestations.insert(aid);
        } else if (tag == "R") {
            std::string chain;
            std::string receipt;
            std::getline(ls, chain, '|');
            std::getline(ls, receipt);
            if (chain.empty() || receipt.empty()) {
                error = "invalid bridge receipt line";
                return false;
            }
            chains_[chain].receipts.push_back(receipt);
        }
    }

    return true;
}

} // namespace addition
