#include "addition/state_store.hpp"

#include "addition/block.hpp"
#include "addition/crypto.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace addition {
namespace {

bool write_text(const std::string& path, const std::string& content, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open for write: " + path;
        return false;
    }
    out << content;
    return true;
}

bool read_text(const std::string& path, std::string& content, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open for read: " + path;
        return false;
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    content = oss.str();
    return true;
}

std::string tx_to_line(const Transaction& tx) {
    std::ostringstream oss;
    oss << "T|" << tx.signer << '|' << tx.signer_pubkey << '|' << tx.signature << '|' << tx.fee << '|' << tx.nonce << '\n';
    for (const auto& in : tx.inputs) {
        oss << "I|" << in.previous_txid << '|' << in.output_index << '\n';
    }
    for (const auto& out : tx.outputs) {
        oss << "O|" << out.recipient << '|' << out.amount << '\n';
    }
    oss << "E\n";
    return oss.str();
}

bool parse_tx_lines(std::istringstream& iss, Transaction& tx, std::string& error) {
    tx = Transaction{};

    for (std::string line; std::getline(iss, line);) {
        if (line == "E") {
            return true;
        }
        if (line.size() < 2 || line[1] != '|') {
            error = "invalid tx line";
            return false;
        }
        const char tag = line[0];
        std::istringstream ls(line.substr(2));

        if (tag == 'I') {
            TxInput in{};
            std::string idx;
            std::getline(ls, in.previous_txid, '|');
            std::getline(ls, idx);
            in.output_index = static_cast<std::uint32_t>(std::stoul(idx));
            tx.inputs.push_back(in);
        } else if (tag == 'O') {
            TxOutput out{};
            std::string amt;
            std::getline(ls, out.recipient, '|');
            std::getline(ls, amt);
            out.amount = static_cast<std::uint64_t>(std::stoull(amt));
            tx.outputs.push_back(out);
        } else if (tag == 'T') {
            std::string fee;
            std::string nonce;
            std::getline(ls, tx.signer, '|');
            std::getline(ls, tx.signer_pubkey, '|');
            std::getline(ls, tx.signature, '|');
            std::getline(ls, fee, '|');
            std::getline(ls, nonce);
            tx.fee = static_cast<std::uint64_t>(std::stoull(fee));
            tx.nonce = static_cast<std::uint64_t>(std::stoull(nonce));
        } else {
            error = "unknown tx tag";
            return false;
        }
    }

    error = "unterminated tx block";
    return false;
}

} // namespace

StateStore::StateStore(std::string data_dir) : data_dir_(std::move(data_dir)) {}

std::string StateStore::blocks_path() const { return data_dir_ + "/blocks.dat"; }
std::string StateStore::mempool_path() const { return data_dir_ + "/mempool.dat"; }
std::string StateStore::staking_path() const { return data_dir_ + "/staking.dat"; }
std::string StateStore::contracts_path() const { return data_dir_ + "/contracts.dat"; }
std::string StateStore::tokens_path() const { return data_dir_ + "/tokens.dat"; }
std::string StateStore::bridge_path() const { return data_dir_ + "/bridge.dat"; }
std::string StateStore::peers_path() const { return data_dir_ + "/peers.dat"; }
std::string StateStore::node_identity_path() const { return data_dir_ + "/node_identity.dat"; }
std::string StateStore::peer_pins_path() const { return data_dir_ + "/peer_pins.dat"; }
std::string StateStore::privacy_path() const { return data_dir_ + "/privacy.dat"; }
std::string StateStore::pouw_storage_path() const { return data_dir_ + "/pouw_storage.dat"; }
std::string StateStore::pouw_compute_path() const { return data_dir_ + "/pouw_compute.dat"; }
std::string StateStore::private_messages_path() const { return data_dir_ + "/private_messages.dat"; }

bool StateStore::save_all(const Chain& chain,
                          const Mempool& mempool,
                          const StakingEngine& staking,
                          const ContractEngine& contracts,
                          const TokenEngine& tokens,
                          const BridgeEngine& bridge,
                          const PeerNetwork& peers,
                          const DecentralizedNode& node,
                          const PoUWStorageEngine& pouw_storage,
                          const PoUWComputeEngine& pouw_compute,
                          const PrivateMessagingEngine& private_messaging,
                          const PrivacyPool& privacy,
                          std::string& error) const {
    std::filesystem::create_directories(data_dir_);

    {
        std::ostringstream blocks;
        for (const auto& b : chain.blocks()) {
            if (b.header.height == 0) {
                continue;
            }
            blocks << "B|" << b.header.height << '|' << b.header.previous_hash << '|' << b.header.timestamp
                   << '|' << b.header.nonce << '|' << b.header.difficulty_target << '|' << b.header.merkle_root
                   << '\n';
            for (const auto& tx : b.transactions) {
                blocks << tx_to_line(tx);
            }
            blocks << "Z\n";
        }
        if (!write_text(blocks_path(), blocks.str(), error)) {
            return false;
        }
    }

    {
        std::ostringstream mp;
        auto txs = mempool.snapshot();
        for (const auto& tx : txs) {
            mp << tx_to_line(tx);
        }
        if (!write_text(mempool_path(), mp.str(), error)) {
            return false;
        }
    }

    {
        std::ostringstream st;
        st << "T|" << staking.total_staked() << '\n';
        for (const auto& [addr, amount] : staking.stakes_map()) {
            st << "S|" << addr << '|' << amount << '\n';
        }
        for (const auto& [addr, amount] : staking.claimable_map()) {
            st << "C|" << addr << '|' << amount << '\n';
        }
        if (!write_text(staking_path(), st.str(), error)) {
            return false;
        }
    }

    {
        if (!write_text(contracts_path(), contracts.dump_state(), error)) {
            return false;
        }
    }

    {
        if (!write_text(tokens_path(), tokens.dump_state(), error)) {
            return false;
        }
    }

    {
        if (!write_text(bridge_path(), bridge.dump_state(), error)) {
            return false;
        }
    }

    {
        std::ostringstream ps;
        for (const auto& p : peers.peers()) {
            ps << p << '\n';
        }
        if (!write_text(peers_path(), ps.str(), error)) {
            return false;
        }
    }

    {
        std::ostringstream id;
        id << "PUB|" << node.node_public_key() << '\n';
        if (!write_text(node_identity_path(), id.str(), error)) {
            return false;
        }
    }

    {
        std::ostringstream pp;
        for (const auto& [peer, pk] : node.peer_pins()) {
            pp << peer << '|' << pk << '\n';
        }
        if (!write_text(peer_pins_path(), pp.str(), error)) {
            return false;
        }
    }

    {
        if (!write_text(privacy_path(), privacy.dump_state(), error)) {
            return false;
        }
    }

    {
        if (!write_text(pouw_storage_path(), pouw_storage.dump_state(), error)) {
            return false;
        }
    }

    {
        if (!write_text(pouw_compute_path(), pouw_compute.dump_state(), error)) {
            return false;
        }
    }

    {
        if (!write_text(private_messages_path(), private_messaging.dump_state(), error)) {
            return false;
        }
    }

    return true;
}

bool StateStore::load_all(Chain& chain,
                          Mempool& mempool,
                          StakingEngine& staking,
                          ContractEngine& contracts,
                          TokenEngine& tokens,
                          BridgeEngine& bridge,
                          PeerNetwork& peers,
                          DecentralizedNode& node,
                          PoUWStorageEngine& pouw_storage,
                          PoUWComputeEngine& pouw_compute,
                          PrivateMessagingEngine& private_messaging,
                          PrivacyPool& privacy,
                          std::string& error) const {
    try {
    std::filesystem::create_directories(data_dir_);

    chain.reset();

    {
        std::string content;
        if (std::filesystem::exists(blocks_path()) && read_text(blocks_path(), content, error)) {
            std::istringstream iss(content);
            for (std::string line; std::getline(iss, line);) {
                if (line.empty()) {
                    continue;
                }
                if (line.rfind("B|", 0) != 0) {
                    continue;
                }

                Block b{};
                {
                    std::istringstream hs(line.substr(2));
                    std::string h;
                    std::getline(hs, h, '|'); b.header.height = std::stoull(h);
                    std::getline(hs, b.header.previous_hash, '|');
                    std::getline(hs, h, '|'); b.header.timestamp = std::stoull(h);
                    std::getline(hs, h, '|'); b.header.nonce = std::stoull(h);
                    std::getline(hs, h, '|'); b.header.difficulty_target = std::stoull(h);
                    std::getline(hs, b.header.merkle_root);
                }

                for (std::string tline; std::getline(iss, tline);) {
                    if (tline == "Z") {
                        break;
                    }
                    if (tline.rfind("T|", 0) == 0) {
                        Transaction tx{};
                        std::ostringstream tx_block;
                        tx_block << tline << '\n';

                        std::string follow;
                        while (std::getline(iss, follow)) {
                            tx_block << follow << '\n';
                            if (follow == "E") {
                                break;
                            }
                        }

                        std::istringstream tx_iss(tx_block.str());
                        if (!parse_tx_lines(tx_iss, tx, error)) {
                            return false;
                        }
                        b.transactions.push_back(tx);
                    }
                }

                std::string add_err;
                if (!chain.add_block(b, add_err)) {
                    error = "failed to replay block: " + add_err;
                    return false;
                }
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(mempool_path()) && read_text(mempool_path(), content, error)) {
            std::istringstream iss(content);
            std::vector<Transaction> restored;
            for (std::string line; std::getline(iss, line);) {
                if (line.rfind("T|", 0) == 0) {
                    std::ostringstream tx_block;
                    tx_block << line << '\n';
                    std::string follow;
                    while (std::getline(iss, follow)) {
                        tx_block << follow << '\n';
                        if (follow == "E") {
                            break;
                        }
                    }
                    Transaction tx{};
                    std::istringstream tx_iss(tx_block.str());
                    if (!parse_tx_lines(tx_iss, tx, error)) {
                        return false;
                    }
                    restored.push_back(std::move(tx));
                }
            }
            mempool.replace(restored);
        }
    }

    {
        std::unordered_map<std::string, std::uint64_t> stakes;
        std::unordered_map<std::string, std::uint64_t> claimable;
        std::uint64_t total = 0;

        std::string content;
        if (std::filesystem::exists(staking_path()) && read_text(staking_path(), content, error)) {
            std::istringstream iss(content);
            for (std::string line; std::getline(iss, line);) {
                if (line.size() < 2 || line[1] != '|') {
                    continue;
                }
                const char tag = line[0];
                std::istringstream ls(line.substr(2));
                if (tag == 'T') {
                    std::string v;
                    std::getline(ls, v);
                    total = std::stoull(v);
                } else if (tag == 'S') {
                    std::string addr, amt;
                    std::getline(ls, addr, '|');
                    std::getline(ls, amt);
                    stakes[addr] = std::stoull(amt);
                } else if (tag == 'C') {
                    std::string addr, amt;
                    std::getline(ls, addr, '|');
                    std::getline(ls, amt);
                    claimable[addr] = std::stoull(amt);
                }
            }
        }

        staking.replace_state(stakes, claimable, total);
    }

    {
        std::string content;
        if (std::filesystem::exists(contracts_path()) && read_text(contracts_path(), content, error)) {
            if (!contracts.load_state(content, error)) {
                return false;
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(tokens_path()) && read_text(tokens_path(), content, error)) {
            if (!tokens.load_state(content, error)) {
                return false;
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(bridge_path()) && read_text(bridge_path(), content, error)) {
            if (!bridge.load_state(content, error)) {
                return false;
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(peers_path()) && read_text(peers_path(), content, error)) {
            std::istringstream iss(content);
            for (std::string line; std::getline(iss, line);) {
                if (!line.empty()) {
                    peers.add_peer(line);
                }
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(node_identity_path()) && read_text(node_identity_path(), content, error)) {
            std::istringstream iss(content);
            std::string pub;
            for (std::string line; std::getline(iss, line);) {
                if (line.rfind("PUB|", 0) == 0) {
                    pub = line.substr(4);
                }
            }
            if (!pub.empty() && pub != node.node_public_key()) {
                error = "node public key mismatch with persisted identity";
                return false;
            }
        }
    }

    {
        std::string content;
        std::vector<std::pair<std::string, std::string>> pins;
        if (std::filesystem::exists(peer_pins_path()) && read_text(peer_pins_path(), content, error)) {
            std::istringstream iss(content);
            for (std::string line; std::getline(iss, line);) {
                if (line.empty()) {
                    continue;
                }
                const auto sep = line.find('|');
                if (sep == std::string::npos || sep == 0 || sep + 1 >= line.size()) {
                    continue;
                }
                pins.emplace_back(line.substr(0, sep), line.substr(sep + 1));
            }
        }
        node.load_peer_pins(pins);
    }

    {
        std::string content;
        if (std::filesystem::exists(privacy_path()) && read_text(privacy_path(), content, error)) {
            if (!privacy.load_state(content, error)) {
                return false;
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(pouw_storage_path()) && read_text(pouw_storage_path(), content, error)) {
            if (!pouw_storage.load_state(content, error)) {
                return false;
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(pouw_compute_path()) && read_text(pouw_compute_path(), content, error)) {
            if (!pouw_compute.load_state(content, error)) {
                return false;
            }
        }
    }

    {
        std::string content;
        if (std::filesystem::exists(private_messages_path()) && read_text(private_messages_path(), content, error)) {
            if (!private_messaging.load_state(content, error)) {
                return false;
            }
        }
    }

    return true;
    } catch (const std::exception& e) {
        error = std::string("state parse exception: ") + e.what();
        return false;
    } catch (...) {
        error = "state parse exception: unknown";
        return false;
    }
}

} // namespace addition
