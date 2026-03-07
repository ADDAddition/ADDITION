#pragma once

#include "addition/chain.hpp"
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
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"

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
              bool strict_admin_mode);

    std::string handle_command(const std::string& line, bool trusted = true);

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
};

} // namespace addition
