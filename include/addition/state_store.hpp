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

private:
    std::string data_dir_;

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
