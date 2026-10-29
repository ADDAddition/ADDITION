cmake_minimum_required(VERSION 3.20)
project(ADDITION_FINAL VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ADDITION_BUILD_TESTS "Build tests" OFF)
option(ADDITION_BUILD_SHIELDED "Build the RISC Zero STARK shielded-pool prover/verifier (Rust, EXPERIMENTAL)" ON)

find_package(OpenSSL REQUIRED)

# Shielded pool proof system: RISC Zero zkVM STARK (hash-based, no trusted setup).
# EXPERIMENTAL / NOT AUDITED. Without it, nodes reject every shielded tx (fail-closed).
set(ADDITION_SHIELDED_CRATE_DIR ${CMAKE_SOURCE_DIR}/zk/shielded_stark)
set(ADDITION_SHIELDED_LIB ${ADDITION_SHIELDED_CRATE_DIR}/target/release/libaddition_shielded_ffi.a)
if(ADDITION_BUILD_SHIELDED)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)
    add_custom_command(
        OUTPUT ${ADDITION_SHIELDED_LIB}
        COMMAND ${CARGO_EXECUTABLE} build --release --manifest-path ${ADDITION_SHIELDED_CRATE_DIR}/Cargo.toml -p addition-shielded-ffi
        WORKING_DIRECTORY ${ADDITION_SHIELDED_CRATE_DIR}
        COMMENT "Building addition-shielded-ffi (RISC Zero STARK)"
        VERBATIM
    )
    add_custom_target(addition_shielded_rust DEPENDS ${ADDITION_SHIELDED_LIB})
endif()

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
    src/wire_v3.cpp
    src/chain.cpp
    src/script.cpp
    src/channel.cpp
    src/consensus_rules.cpp
    src/mempool.cpp
    src/miner.cpp
    src/rpc_server.cpp
    src/rpc_network_server.cpp
    src/rpc_access.cpp
    src/state_store.cpp
    src/wallet_keys.cpp
    src/wallet_store.cpp
    src/staking.cpp
    src/contract_engine.cpp
    src/token_engine.cpp
    src/bridge.cpp
    src/decentralized_node.cpp
    src/net_io.cpp
    src/net_transport.cpp
    src/dandelion.cpp
    src/p2p.cpp
    src/consensus_engine.cpp
    src/privacy.cpp
    src/shielded.cpp
    src/fast_path.cpp
    src/pouw_storage.cpp
    src/pouw_compute.cpp
    src/private_messaging.cpp
    src/ai_optimizer.cpp
    src/wallet.cpp
    src/auto_mine.cpp
    src/btc_hygiene.cpp
)

target_include_directories(addition_core PUBLIC include)
target_include_directories(addition_core PUBLIC ${OQS_INCLUDE_DIR})
target_link_libraries(addition_core PUBLIC ${OQS_LIBRARY} OpenSSL::SSL OpenSSL::Crypto)

if(ADDITION_BUILD_SHIELDED)
    target_include_directories(addition_core PUBLIC ${ADDITION_SHIELDED_CRATE_DIR}/include)
    target_compile_definitions(addition_core PUBLIC ADDITION_HAVE_SHIELDED_STARK=1)
    add_dependencies(addition_core addition_shielded_rust)
    target_link_libraries(addition_core PUBLIC ${ADDITION_SHIELDED_LIB} pthread dl m stdc++)
    set_source_files_properties(src/shielded.cpp PROPERTIES OBJECT_DEPENDS ${ADDITION_SHIELDED_LIB})
endif()

if(WIN32)
    target_link_libraries(addition_core PUBLIC ws2_32)
endif()

add_executable(additiond apps/additiond_main.cpp)
target_link_libraries(additiond PRIVATE addition_core)

if(ADDITION_BUILD_TESTS)
    enable_testing()
    add_executable(test_chain tests/test_chain.cpp)
    target_link_libraries(test_chain PRIVATE addition_core)
    add_test(NAME test_chain COMMAND test_chain)
    add_executable(test_channels tests/test_channels.cpp)
    target_link_libraries(test_channels PRIVATE addition_core)
    add_test(NAME test_channels COMMAND test_channels)
    set_tests_properties(test_channels PROPERTIES TIMEOUT 300)
    add_executable(test_channel_rpc tests/test_channel_rpc.cpp)
    target_link_libraries(test_channel_rpc PRIVATE addition_core)
    add_test(NAME test_channel_rpc COMMAND test_channel_rpc)

    add_executable(test_net_tor tests/test_net_tor.cpp)
    target_link_libraries(test_net_tor PRIVATE addition_core)
    add_test(NAME test_net_tor COMMAND test_net_tor)

    add_executable(test_speed_v3 tests/test_speed_v3.cpp)
    target_link_libraries(test_speed_v3 PRIVATE addition_core)
    add_test(NAME test_speed_v3 COMMAND test_speed_v3)
    set_tests_properties(test_speed_v3 PROPERTIES TIMEOUT 120)

    add_executable(bench_throughput tests/bench_throughput.cpp)
    target_link_libraries(bench_throughput PRIVATE addition_core)
    add_executable(test_consensus_rules tests/test_consensus_rules.cpp)
    target_link_libraries(test_consensus_rules PRIVATE addition_core)
    add_test(NAME test_consensus_rules COMMAND test_consensus_rules)

    add_executable(test_staking tests/test_staking.cpp)
    target_link_libraries(test_staking PRIVATE addition_core)
    add_test(NAME test_staking COMMAND test_staking)

    add_executable(test_mainnet_chain tests/test_mainnet_chain.cpp)
    target_link_libraries(test_mainnet_chain PRIVATE addition_core)
    add_test(NAME test_mainnet_chain COMMAND test_mainnet_chain)
    set_tests_properties(test_mainnet_chain PROPERTIES TIMEOUT 90)

    add_executable(test_rpc_access tests/test_rpc_access.cpp)
    target_link_libraries(test_rpc_access PRIVATE addition_core)
    add_test(NAME test_rpc_access COMMAND test_rpc_access)

    add_executable(test_public_read_during_mine tests/test_public_read_during_mine.cpp)
    target_link_libraries(test_public_read_during_mine PRIVATE addition_core)
    add_test(NAME test_public_read_during_mine COMMAND test_public_read_during_mine)
    set_tests_properties(test_public_read_during_mine PROPERTIES TIMEOUT 60)

    add_executable(test_wallet tests/test_wallet.cpp)
    target_link_libraries(test_wallet PRIVATE addition_core)
    add_test(NAME test_wallet COMMAND test_wallet)

    add_executable(test_hash_address tests/test_hash_address.cpp)
    target_link_libraries(test_hash_address PRIVATE addition_core)
    add_test(NAME test_hash_address COMMAND test_hash_address)

    add_executable(test_crypto tests/test_crypto.cpp)
    target_link_libraries(test_crypto PRIVATE addition_core)
    add_test(NAME test_crypto COMMAND test_crypto)
    set_tests_properties(test_crypto PROPERTIES TIMEOUT 60)

    add_executable(test_slh_dsa tests/test_slh_dsa.cpp)
    target_link_libraries(test_slh_dsa PRIVATE addition_core)
    add_test(NAME test_slh_dsa COMMAND test_slh_dsa)
    set_tests_properties(test_slh_dsa PROPERTIES TIMEOUT 180)

    add_executable(test_btc_hygiene tests/test_btc_hygiene.cpp)
    target_link_libraries(test_btc_hygiene PRIVATE addition_core)
    add_test(NAME test_btc_hygiene COMMAND test_btc_hygiene)
    set_tests_properties(test_btc_hygiene PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(test_privacy tests/test_privacy.cpp)
    target_link_libraries(test_privacy PRIVATE addition_core)
    add_test(NAME test_privacy COMMAND test_privacy)

    add_executable(test_shielded tests/test_shielded.cpp)
    target_link_libraries(test_shielded PRIVATE addition_core)
    add_test(NAME test_shielded COMMAND test_shielded)
    set_tests_properties(test_shielded PROPERTIES TIMEOUT 3600)

    add_executable(test_fast_path tests/test_fast_path.cpp)
    target_link_libraries(test_fast_path PRIVATE addition_core)
    add_test(NAME test_fast_path COMMAND test_fast_path)
    set_tests_properties(test_fast_path PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(test_auto_mine tests/test_auto_mine.cpp)
    target_link_libraries(test_auto_mine PRIVATE addition_core)
    add_test(NAME test_auto_mine COMMAND test_auto_mine)

    add_executable(test_confirmations tests/test_confirmations.cpp)
    target_link_libraries(test_confirmations PRIVATE addition_core)
    add_test(NAME test_confirmations COMMAND test_confirmations)

    add_executable(test_p2p_sync tests/test_p2p_sync.cpp)
    target_link_libraries(test_p2p_sync PRIVATE addition_core)
    add_test(NAME test_p2p_sync COMMAND test_p2p_sync)
    set_tests_properties(test_p2p_sync PROPERTIES TIMEOUT 120)

    add_executable(test_runtime tests/test_runtime.cpp)
    target_link_libraries(test_runtime PRIVATE addition_core)
    add_test(NAME test_runtime COMMAND test_runtime)
    set_tests_properties(test_runtime PROPERTIES TIMEOUT 90)

    add_executable(test_amm tests/test_amm.cpp)
    target_link_libraries(test_amm PRIVATE addition_core)
    add_test(NAME test_amm COMMAND test_amm)
    set_tests_properties(test_amm PROPERTIES TIMEOUT 90)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME test_public_rpc_two_node
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_public_rpc_two_node.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_public_rpc_two_node PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 180)
        add_test(
            NAME test_two_node_http_ingest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_two_node_http_ingest.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_two_node_http_ingest PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 180)
        add_test(
            NAME test_mainnet_two_node
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_mainnet_two_node.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_mainnet_two_node PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 900)
        add_test(
            NAME test_live_mine_and_privacy
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_live_mine_and_privacy.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_live_mine_and_privacy PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 90)
        add_test(
            NAME test_chain_persist_restart
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_chain_persist_restart.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_chain_persist_restart PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 90)
        add_test(
            NAME test_auto_mine_daemon
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_auto_mine_daemon.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_auto_mine_daemon PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 90)
        add_test(
            NAME test_public_site_static
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_public_site_static.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME test_bridge_spec
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_bridge_spec.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME test_wallet_packaging
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_wallet_packaging.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME test_runtime_live
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_runtime_live.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_runtime_live PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 180)
        add_test(
            NAME test_amm_live
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_amm_live.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_amm_live PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 90)
        add_test(
            NAME test_jsonrpc_adapter
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_jsonrpc_adapter.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME test_mainnet_genesis
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_mainnet_genesis.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_mainnet_genesis PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 180)
        add_test(
            NAME test_evm_rpc_bridge
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_evm_rpc_bridge.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME test_core_path_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_core_path_smoke.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_core_path_smoke PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 180)
        add_test(
            NAME test_regtest_two_node
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_regtest_two_node.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_regtest_two_node PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 120)
        add_test(
            NAME test_regtest_staking_three_node
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_regtest_staking_three_node.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_regtest_staking_three_node PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>"
            TIMEOUT 300)
    endif()
endif()
