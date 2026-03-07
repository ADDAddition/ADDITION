#include "addition/privacy.hpp"

#include "addition/crypto.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <sstream>

namespace addition {
namespace {

bool is_hex_even(const std::string& s);

bool is_mainnet_mode_enabled() {
    if (const char* v = std::getenv("ADDITION_MAINNET_MODE")) {
        return std::string(v) == "1";
    }
    return false;
}

std::string mk_note_id(const std::string& owner,
                       std::uint64_t amount,
                       const std::string& seed) {
    return to_hex(sha3_512_bytes("note|" + owner + "|" + std::to_string(amount) + "|" + seed));
}

bool get_privacy_master_key(std::string& out, std::string& error) {
    const char* mk = std::getenv("ADDITION_PRIVACY_MASTER_KEY");
    if (mk == nullptr || std::string(mk).empty()) {
        error = "ADDITION_PRIVACY_MASTER_KEY not set";
        return false;
    }
    out = mk;
    return true;
}

bool owner_tag_for(const std::string& owner, std::string& out, std::string& error) {
    std::string mk;
    if (!get_privacy_master_key(mk, error)) {
        return false;
    }
    out = to_hex(sha3_512_bytes("owner_tag|" + mk + "|" + owner));
    return true;
}

bool derive_amount_mask64(const std::string& note_id,
                          const std::string& blinding,
                          std::uint64_t& mask64,
                          std::string& error) {
    std::string mk;
    if (!get_privacy_master_key(mk, error)) {
        return false;
    }
    const auto mask_hex = to_hex(sha3_512_bytes("amount_mask|" + mk + "|" + note_id + "|" + blinding));
    mask64 = static_cast<std::uint64_t>(std::stoull(mask_hex.substr(0, 16), nullptr, 16));
    return true;
}

bool seal_amount(std::uint64_t amount,
                 const std::string& note_id,
                 const std::string& blinding,
                 std::string& sealed_hex,
                 std::string& error) {
    std::uint64_t mask64 = 0;
    if (!derive_amount_mask64(note_id, blinding, mask64, error)) {
        return false;
    }
    const auto sealed = amount ^ mask64;
    std::ostringstream oss;
    oss << std::hex << sealed;
    sealed_hex = oss.str();
    return true;
}

bool unseal_amount(const std::string& sealed_hex,
                   const std::string& note_id,
                   const std::string& blinding,
                   std::uint64_t& out,
                   std::string& error) {
    if (!is_hex_even((sealed_hex.size() % 2 == 0) ? sealed_hex : (std::string("0") + sealed_hex))) {
        error = "invalid sealed amount";
        return false;
    }
    std::uint64_t mask64 = 0;
    if (!derive_amount_mask64(note_id, blinding, mask64, error)) {
        return false;
    }
    try {
        const auto sealed = static_cast<std::uint64_t>(std::stoull(sealed_hex, nullptr, 16));
        out = sealed ^ mask64;
        return true;
    } catch (const std::exception&) {
        error = "invalid sealed amount parse";
        return false;
    }
}

bool is_hex_even(const std::string& s) {
    if (s.empty() || (s.size() % 2) != 0) {
        return false;
    }
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

bool PrivacyPool::set_native_verifier_mode(const std::string& mode, std::string& error) {
    const auto normalized = lower_copy(mode);
    if (normalized == "pq_mldsa87" || normalized == "pq" || normalized == "mldsa87") {
        native_verifier_mode_ = NativeVerifierMode::pq_mldsa87;
        return true;
    }

    error = "unsupported native verifier mode (use: pq_mldsa87)";
    return false;
}

std::string PrivacyPool::native_verifier_mode() const {
    switch (native_verifier_mode_) {
        case NativeVerifierMode::pq_mldsa87:
            return "pq_mldsa87";
        default:
            return "pq_mldsa87";
    }
}

bool PrivacyPool::strict_zk_mode() const {
    return strict_zk_mode_;
}

bool PrivacyPool::verify_native_proof(const std::string& public_input,
                                      const std::string& proof_hex,
                                      const std::string& verification_key_hex,
                                      std::string& error) const {
    if (native_verifier_mode_ == NativeVerifierMode::pq_mldsa87) {
        if (verify_message_signature_hybrid(verification_key_hex,
                                            public_input,
                                            std::string("pq=") + proof_hex,
                                            "ml-dsa-87")) {
            return true;
        }
        error = "native verifier rejected proof";
        return false;
    }

    error = "unknown native verifier mode";
    return false;
}

bool PrivacyPool::verify_proof(const std::string& public_input,
                               const std::string& proof_hex,
                               const std::string& verification_key_hex,
                               std::string& error) const {
    if (!is_hex_even(proof_hex) || !is_hex_even(verification_key_hex)) {
        error = "proof/vk must be even-length hex";
        return false;
    }

    if (public_input.find('"') != std::string::npos) {
        error = "public input contains invalid quote";
        return false;
    }
    if (public_input.find('\n') != std::string::npos || public_input.find('\r') != std::string::npos) {
        error = "public input contains invalid newline";
        return false;
    }

    return verify_native_proof(public_input, proof_hex, verification_key_hex, error);
}

std::string PrivacyPool::mint_note_internal(const std::string& owner,
                                            std::uint64_t amount,
                                            std::string& error) {
    if (owner.empty()) {
        error = "owner empty";
        return {};
    }
    if (amount == 0) {
        error = "amount must be > 0";
        return {};
    }

    const auto blind = to_hex(sha3_512_bytes("blind|" + owner + "|" + std::to_string(notes_.size())));
    const auto note_id = mk_note_id(owner, amount, blind);
    const auto commitment = to_hex(sha3_512_bytes("cm|" + blind + "|" + std::to_string(amount)));
    const auto nullifier = to_hex(sha3_512_bytes("nf|" + note_id));

    std::string sealed_amount;
    if (!seal_amount(amount, note_id, blind, sealed_amount, error)) {
        return {};
    }

    std::string owner_tag;
    if (!owner_tag_for(owner, owner_tag, error)) {
        return {};
    }

    notes_[note_id] = PrivateNote{owner_tag,
                                  sealed_amount,
                                  blind,
                                  commitment,
                                  nullifier,
                                  false};
    return note_id;
}

std::string PrivacyPool::mint_zk(const std::string& owner,
                                 std::uint64_t amount,
                                 const std::string& commitment,
                                 const std::string& nullifier,
                                 const std::string& proof_hex,
                                 const std::string& verification_key_hex,
                                 std::string& error) {
    if (owner.empty() || amount == 0) {
        error = "invalid mint_zk params";
        return {};
    }
    if (!is_hex_even(commitment) || !is_hex_even(nullifier)) {
        error = "commitment/nullifier must be even-length hex";
        return {};
    }
    if (used_nullifiers_.count(nullifier)) {
        error = "nullifier already used";
        return {};
    }

    const std::string public_input = "mint|" + owner + "|" + std::to_string(amount) + "|" + commitment + "|" +
                                     nullifier;
    if (!verify_proof(public_input, proof_hex, verification_key_hex, error)) {
        return {};
    }

    const auto note_id = mk_note_id(owner, amount, commitment);
    if (notes_.count(note_id)) {
        error = "note already exists";
        return {};
    }

    std::string sealed_amount;
    if (!seal_amount(amount, note_id, "zk", sealed_amount, error)) {
        return {};
    }

    std::string owner_tag;
    if (!owner_tag_for(owner, owner_tag, error)) {
        return {};
    }

    notes_[note_id] = PrivateNote{owner_tag,
                                  sealed_amount,
                                  "zk",
                                  commitment,
                                  nullifier,
                                  false};
    return note_id;
}

bool PrivacyPool::spend_zk(const std::string& owner,
                           const std::string& note_id,
                           const std::string& recipient,
                           std::uint64_t amount,
                           const std::string& nullifier,
                           const std::string& proof_hex,
                           const std::string& verification_key_hex,
                           std::string& new_note_id,
                           std::string& error) {
    if (owner.empty() || recipient.empty() || note_id.empty() || amount == 0) {
        error = "invalid spend_zk params";
        return false;
    }

    auto it = notes_.find(note_id);
    if (it == notes_.end()) {
        error = "note not found";
        return false;
    }
    auto& note = it->second;
    if (note.spent) {
        error = "note already spent";
        return false;
    }
    std::string expected_owner_tag;
    if (!owner_tag_for(owner, expected_owner_tag, error)) {
        return false;
    }
    if (note.owner_tag != expected_owner_tag) {
        error = "owner mismatch";
        return false;
    }
    std::uint64_t note_amount = 0;
    if (!unseal_amount(note.amount_sealed, note_id, note.blinding, note_amount, error)) {
        return false;
    }
    if (note_amount < amount) {
        error = "insufficient note amount";
        return false;
    }
    if (note.nullifier != nullifier) {
        error = "nullifier mismatch";
        return false;
    }
    if (used_nullifiers_.count(nullifier)) {
        error = "nullifier already used";
        return false;
    }

    const std::string public_input = "spend|" + owner + "|" + note_id + "|" + recipient + "|" +
                                     std::to_string(amount) + "|" + nullifier;
    if (!verify_proof(public_input, proof_hex, verification_key_hex, error)) {
        return false;
    }

    note.spent = true;
    used_nullifiers_.insert(nullifier);

    std::string mint_error;
    new_note_id = mint_note_internal(recipient, amount, mint_error);
    if (!mint_error.empty()) {
        note.spent = false;
        used_nullifiers_.erase(nullifier);
        error = mint_error;
        return false;
    }

    const auto change = note_amount - amount;
    if (change > 0) {
        std::string change_note;
        change_note = mint_note_internal(owner, change, mint_error);
        if (!mint_error.empty() || change_note.empty()) {
            note.spent = false;
            used_nullifiers_.erase(nullifier);
            notes_.erase(new_note_id);
            new_note_id.clear();
            error = "failed to mint change note";
            return false;
        }
    }

    return true;
}

bool PrivacyPool::verifier_configured() const {
    return true;
}

std::size_t PrivacyPool::note_count() const {
    return notes_.size();
}

std::size_t PrivacyPool::used_nullifier_count() const {
    return used_nullifiers_.size();
}

std::string PrivacyPool::dump_state() const {
    std::ostringstream oss;
    oss << "M|" << (strict_zk_mode_ ? 1 : 0) << '\n';
    oss << "Z|" << native_verifier_mode() << '\n';
    for (const auto& [id, n] : notes_) {
        oss << "N|" << id << '|' << n.owner_tag << '|' << n.amount_sealed << '|' << n.blinding << '|' << n.commitment
            << '|' << n.nullifier << '|' << (n.spent ? 1 : 0) << '\n';
    }
    for (const auto& nf : used_nullifiers_) {
        oss << "U|" << nf << '\n';
    }
    return oss.str();
}

bool PrivacyPool::load_state(const std::string& state, std::string& error) {
    notes_.clear();
    used_nullifiers_.clear();
    native_verifier_mode_ = NativeVerifierMode::pq_mldsa87;
    strict_zk_mode_ = true;

    std::istringstream iss(state);
    for (std::string line; std::getline(iss, line);) {
        if (line.empty()) {
            continue;
        }
        if (line.size() < 2 || line[1] != '|') {
            continue;
        }
        const char tag = line[0];
        std::istringstream ls(line.substr(2));

        if (tag == 'Z') {
            std::string mode;
            std::getline(ls, mode);
            std::string mode_error;
            if (!set_native_verifier_mode(mode, mode_error)) {
                error = "invalid native verifier mode in state";
                return false;
            }
        } else if (tag == 'V') {
            // Backward compatibility: ignore old external verifier command state.
            continue;
        } else if (tag == 'M') {
            std::string v;
            std::getline(ls, v);
            strict_zk_mode_ = (v == "1" || v == "true");
        } else if (tag == 'N') {
            std::string id, owner_or_tag, amt_or_sealed, blind, cm, nf, spent;
            std::getline(ls, id, '|');
            std::getline(ls, owner_or_tag, '|');
            std::getline(ls, amt_or_sealed, '|');
            std::getline(ls, blind, '|');
            std::getline(ls, cm, '|');
            std::getline(ls, nf, '|');
            std::getline(ls, spent);
            if (id.empty()) {
                error = "invalid note line";
                return false;
            }
            if (owner_or_tag.size() == 128 && is_hex_even(owner_or_tag) && is_hex_even(amt_or_sealed)) {
                notes_[id] = PrivateNote{owner_or_tag,
                                         lower_copy(amt_or_sealed),
                                         blind,
                                         cm,
                                         nf,
                                         spent == "1"};
            } else {
                // Backward compatibility migration from plaintext owner/amount state.
                std::string migrated_owner_tag;
                if (!owner_tag_for(owner_or_tag, migrated_owner_tag, error)) {
                    return false;
                }
                std::uint64_t migrated_amount = 0;
                try {
                    migrated_amount = static_cast<std::uint64_t>(std::stoull(amt_or_sealed));
                } catch (const std::exception&) {
                    error = "invalid legacy note amount";
                    return false;
                }
                std::string sealed_amount;
                if (!seal_amount(migrated_amount, id, blind, sealed_amount, error)) {
                    return false;
                }
                notes_[id] = PrivateNote{migrated_owner_tag, sealed_amount, blind, cm, nf, spent == "1"};
            }
        } else if (tag == 'U') {
            std::string nf;
            std::getline(ls, nf);
            if (!nf.empty()) {
                used_nullifiers_.insert(nf);
            }
        }
    }

    if (is_mainnet_mode_enabled()) {
        strict_zk_mode_ = true;
    }

    return true;
}

} // namespace addition
