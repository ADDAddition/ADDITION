#pragma once

#include "addition/bridge.hpp"
#include "addition/chain.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/contract_engine.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/mempool.hpp"
#include "addition/p2p.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/private_messaging.hpp"
#include "addition/privacy.hpp"
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"

#include <string>

namespace addition {

class StateStore {
public:
    explicit StateStore(std::string data_dir);

    bool save_all(const Chain& chain,
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
                  std::string& error) const;

    bool load_all(Chain& chain,
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
                  std::string& error) const;

    // Text block index under data-dir/blocks.dat. UTXOs are rebuilt by replay.
    bool save_chain(const Chain& chain, std::string& error) const;
    bool load_chain(Chain& chain, std::string& error) const;
    bool chain_file_exists() const;

    // tokens/privacy/staking and other side ledgers. Call after a successful write.
    bool save_side_state(const StakingEngine& staking,
                         const ContractEngine& contracts,
                         const TokenEngine& tokens,
                         const BridgeEngine& bridge,
                         const PrivacyPool& privacy,
                         const PoUWStorageEngine& pouw_storage,
                         const PoUWComputeEngine& pouw_compute,
                         const PrivateMessagingEngine& private_messaging,
                         std::string& error) const;

    // Sidecar so a height-0 data dir cannot be reused across networks.
    bool ensure_network_marker(const Chain& chain, std::string& error) const;
    bool write_network_marker(const Chain& chain, std::string& error) const;
    bool check_network_marker(const Chain& chain, std::string& error) const;

private:
    std::string data_dir_;

    std::string network_path() const;
    std::string blocks_path() const;
    std::string mempool_path() const;
    std::string staking_path() const;
    std::string contracts_path() const;
    std::string tokens_path() const;
    std::string bridge_path() const;
    std::string peers_path() const;
    std::string node_identity_path() const;
    std::string peer_pins_path() const;
    std::string privacy_path() const;
    std::string pouw_storage_path() const;
    std::string pouw_compute_path() const;
    std::string private_messages_path() const;
};

} // namespace addition
