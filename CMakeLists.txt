cmake_minimum_required(VERSION 3.20)
project(ADDITION_FINAL VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ADDITION_BUILD_TESTS "Build tests" OFF)

find_package(OpenSSL REQUIRED)

find_path(OQS_INCLUDE_DIR oqs/oqs.h)
find_library(OQS_LIBRARY oqs)

if(OQS_INCLUDE_DIR AND OQS_LIBRARY)
    message(STATUS "liboqs found: ${OQS_LIBRARY}")
else()
    message(FATAL_ERROR "liboqs not found. Install liboqs before building ADDITION_FINAL")
endif()

add_library(addition_core
    src/config.cpp
    src/crypto.cpp
    src/block.cpp
    src/chain.cpp
    src/mempool.cpp
    src/miner.cpp
    src/rpc_server.cpp
    src/rpc_network_server.cpp
    src/state_store.cpp
    src/wallet_keys.cpp
    src/staking.cpp
    src/contract_engine.cpp
    src/token_engine.cpp
    src/bridge.cpp
    src/decentralized_node.cpp
    src/p2p.cpp
    src/consensus_engine.cpp
    src/privacy.cpp
    src/pouw_storage.cpp
    src/pouw_compute.cpp
    src/private_messaging.cpp
    src/ai_optimizer.cpp
    src/wallet.cpp
)

target_include_directories(addition_core PUBLIC include)
target_include_directories(addition_core PUBLIC ${OQS_INCLUDE_DIR})
target_link_libraries(addition_core PUBLIC ${OQS_LIBRARY} OpenSSL::SSL OpenSSL::Crypto)

if(WIN32)
    target_link_libraries(addition_core PUBLIC ws2_32)
endif()

add_executable(additiond apps/additiond_main.cpp)
target_link_libraries(additiond PRIVATE addition_core)

# Tests intentionally disabled in strict final profile.
