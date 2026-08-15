#include "addition/wallet_store.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace addition {
namespace {

bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

void restrict_owner_only(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write |
                                     std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace,
                                 ec);
}

void restrict_owner_rw(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 ec);
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    if (i > 0) {
        s.erase(0, i);
    }
    return s;
}

} // namespace

WalletStore::WalletStore(std::string dir) : dir_(std::move(dir)) {}

bool WalletStore::configured() const {
    return !dir_.empty();
}

const std::string& WalletStore::dir() const {
    return dir_;
}

bool WalletStore::valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    if (!std::isalnum(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    for (char c : name) {
        if (!is_name_char(c)) {
            return false;
        }
    }
    return true;
}

std::string WalletStore::file_path(const std::string& name) const {
    return (std::filesystem::path(dir_) / (name + ".wal")).string();
}

bool WalletStore::ensure_dir(std::string& error) const {
    if (!configured()) {
        error = "wallet store not configured";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        error = "cannot create wallet directory";
        return false;
    }
    restrict_owner_only(dir_);
    return true;
}

bool WalletStore::exists(const std::string& name) const {
    if (!configured() || !valid_name(name)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(file_path(name), ec);
}

bool WalletStore::create(const std::string& name, const WalletKeys& keys, StoredWallet& out, std::string& error) {
    if (!valid_name(name)) {
        error = "invalid wallet name (use 1-64 letters, digits, _ or -)";
        return false;
    }
    if (keys.address.empty() || keys.public_key.empty() || keys.private_key.empty()) {
        error = "incomplete wallet keys";
        return false;
    }
    if (!ensure_dir(error)) {
        return false;
    }
    if (exists(name)) {
        error = "wallet already exists";
        return false;
    }

    const auto path = file_path(name);
    {
        std::ofstream out_file(path, std::ios::binary | std::ios::trunc);
        if (!out_file) {
            error = "cannot write wallet file";
            return false;
        }
        out_file << "# addition-wallet-v1\n"
                 << "name=" << name << '\n'
                 << "algorithm=" << (keys.algorithm.empty() ? "ml-dsa-87" : keys.algorithm) << '\n'
                 << "address=" << keys.address << '\n'
                 << "public_key=" << keys.public_key << '\n'
                 << "private_key=" << keys.private_key << '\n';
        if (!out_file) {
            error = "wallet file write failed";
            return false;
        }
    }
    restrict_owner_rw(path);

    out = StoredWallet{};
    out.name = name;
    out.address = keys.address;
    out.public_key = keys.public_key;
    out.algorithm = keys.algorithm.empty() ? "ml-dsa-87" : keys.algorithm;
    out.path = path;
    return true;
}

bool WalletStore::load(const std::string& name, StoredWallet& out, std::string& error, bool include_private) const {
    if (!valid_name(name)) {
        error = "invalid wallet name";
        return false;
    }
    if (!configured()) {
        error = "wallet store not configured";
        return false;
    }

    const auto path = file_path(name);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "wallet not found";
        return false;
    }

    StoredWallet loaded{};
    loaded.name = name;
    loaded.path = path;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        if (key == "address") {
            loaded.address = value;
        } else if (key == "public_key") {
            loaded.public_key = value;
        } else if (key == "private_key") {
            loaded.private_key = value;
        } else if (key == "algorithm") {
            loaded.algorithm = value;
        } else if (key == "name" && !value.empty()) {
            loaded.name = value;
        }
    }

    if (loaded.address.empty() || loaded.public_key.empty()) {
        error = "wallet file missing address or public_key";
        return false;
    }
    if (include_private && loaded.private_key.empty()) {
        error = "wallet file missing private_key";
        return false;
    }
    if (loaded.algorithm.empty()) {
        loaded.algorithm = "ml-dsa-87";
    }
    if (!include_private) {
        loaded.private_key.clear();
    }
    out = std::move(loaded);
    return true;
}

std::vector<StoredWallet> WalletStore::list(std::string& error) const {
    std::vector<StoredWallet> out;
    if (!configured()) {
        error = "wallet store not configured";
        return out;
    }
    std::error_code ec;
    if (!std::filesystem::exists(dir_, ec)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
        if (ec) {
            error = "cannot read wallet directory";
            return {};
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".wal") {
            continue;
        }
        const auto name = entry.path().stem().string();
        StoredWallet w{};
        std::string load_error;
        if (load(name, w, load_error, false)) {
            out.push_back(std::move(w));
        }
    }
    return out;
}

} // namespace addition
