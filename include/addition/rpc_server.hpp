#pragma once

#include "addition/chain.hpp"
#include "addition/channel.hpp"
#include "addition/bridge.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/contract_engine.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/ai_optimizer.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/p2p.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/private_messaging.hpp"
#include "addition/privacy.hpp"
#include "addition/shielded.hpp"
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"
#include "addition/wallet_store.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace addition {

class RpcServer {
public:
    RpcServer(Chain& chain,
              Mempool& mempool,
              Miner& miner,
              StakingEngine& staking,
              ContractEngine& contracts,
              BridgeEngine& bridge,
              TokenEngine& tokens,
              PeerNetwork& peers,
              ConsensusEngine& consensus,
              PrivacyPool& privacy,
              PoUWStorageEngine& pouw_storage,
              PoUWComputeEngine& pouw_compute,
              PrivateMessagingEngine& private_messaging,
              AIRoutingOptimizer& ai_optimizer,
              DecentralizedNode& node,
              bool allow_insecure_tx_commands,
              bool strict_admin_mode,
              std::string wallet_dir = {});

    std::string handle_command(const std::string& line, bool trusted = true);
    void set_auto_mine_status(bool enabled, std::uint32_t interval_sec);
    void set_advertised_p2p(std::string endpoint);
    std::uint64_t unlocked_balance(const std::string& address) const;
    // Banner for GET / and /health. Uses the live chain network_mode; no RPC lock.
    std::string public_rpc_banner() const;

private:
    Chain& chain_;
    Mempool& mempool_;
    Miner& miner_;
    StakingEngine& staking_;
    ContractEngine& contracts_;
    BridgeEngine& bridge_;
    TokenEngine& tokens_;
    PeerNetwork& peers_;
    ConsensusEngine& consensus_;
    PrivacyPool& privacy_;
    PoUWStorageEngine& pouw_storage_;
    PoUWComputeEngine& pouw_compute_;
    PrivateMessagingEngine& private_messaging_;
    AIRoutingOptimizer& ai_optimizer_;
    DecentralizedNode& node_;
    bool allow_insecure_tx_commands_{false};
    bool strict_admin_mode_{true};
    bool auto_mine_enabled_{false};
    std::uint32_t auto_mine_interval_sec_{60};
    std::string advertised_p2p_{};
    WalletStore wallets_;
    // Payment channels between two wallets held by THIS node (loopback peers;
    // no channel P2P transport yet). In-memory only: lost on restart.
    struct LocalChannel {
        std::string wallet_a, wallet_b;
        std::unique_ptr<channel::Channel> a, b;
        std::string status{"funding"};
    };
    std::map<std::string, LocalChannel> channels_;
    // Cross-node channels: this node holds one side; the other side is a wallet
    // on another node reached over P2P (CHAN| messages). Persisted under
    // <data>/rchannels (0600) and reloaded on first use after restart.
    struct RemoteChannel {
        std::string wallet, peer, status{"open"};
        std::unique_ptr<channel::Channel> ch;
        bool busy{false};
    };
    std::map<std::string, RemoteChannel> rchannels_;
    std::map<std::string, RemoteChannel> rchan_opening_;  // responder side before FUND
    bool rchan_loaded_{false};
    std::string rchan_dir() const;
    void rchan_load();
    bool rchan_save(const std::string& id, const RemoteChannel& rc, std::string& error);
    bool load_wallet_keys(const std::string& name, WalletKeys& keys, std::string& error);
    std::string handle_rchan_command(const std::string& cmd, std::istringstream& iss);
public:
    bool handle_channel_message(const std::string& peer, const std::string& body, std::string& reply,
                                std::string& error);
private:
    // Shielded (addz) spending keys per local wallet name. Testnet tooling:
    // in-memory only (lost on restart); notes are rescanned from the chain.
    std::map<std::string, shielded::SpendingKey> zkeys_;
    bool ensure_zkey(const std::string& name, bool create, std::string& error);
    std::string shielded_scan(const std::string& wallet, std::vector<shielded::Note>* unspent, std::uint64_t* total);
    mutable std::mutex mu_;
public:
    // Shared node-state lock (RPC, inbound P2P, background sync).
    std::mutex& state_mutex() const { return mu_; }
private:
};

} // namespace addition
