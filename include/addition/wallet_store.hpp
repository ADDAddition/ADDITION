#pragma once

#include "addition/wallet_keys.hpp"

#include <string>
#include <vector>

namespace addition {

struct StoredWallet {
    std::string name;
    std::string address;
    std::string public_key;
    std::string private_key;
    std::string algorithm;
    std::string path;
};

class WalletStore {
public:
    explicit WalletStore(std::string dir);

    bool configured() const;
    const std::string& dir() const;

    static bool valid_name(const std::string& name);

    bool create(const std::string& name, const WalletKeys& keys, StoredWallet& out, std::string& error);
    bool load(const std::string& name, StoredWallet& out, std::string& error, bool include_private) const;
    bool exists(const std::string& name) const;
    std::vector<StoredWallet> list(std::string& error) const;

private:
    std::string dir_;

    std::string file_path(const std::string& name) const;
    bool ensure_dir(std::string& error) const;
};

} // namespace addition
