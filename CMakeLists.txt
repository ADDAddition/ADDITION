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
    src/rpc_access.cpp
    src/state_store.cpp
    src/wallet_keys.cpp
    src/wallet_store.cpp
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

if(ADDITION_BUILD_TESTS)
    enable_testing()
    add_executable(test_chain tests/test_chain.cpp)
    target_link_libraries(test_chain PRIVATE addition_core)
    add_test(NAME test_chain COMMAND test_chain)

    add_executable(test_rpc_access tests/test_rpc_access.cpp)
    target_link_libraries(test_rpc_access PRIVATE addition_core)
    add_test(NAME test_rpc_access COMMAND test_rpc_access)

    add_executable(test_wallet tests/test_wallet.cpp)
    target_link_libraries(test_wallet PRIVATE addition_core)
    add_test(NAME test_wallet COMMAND test_wallet)

    add_executable(test_privacy tests/test_privacy.cpp)
    target_link_libraries(test_privacy PRIVATE addition_core)
    add_test(NAME test_privacy COMMAND test_privacy)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME test_public_rpc_two_node
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_public_rpc_two_node.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(test_public_rpc_two_node PROPERTIES
            ENVIRONMENT "ADDITIOND=$<TARGET_FILE:additiond>")
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
    endif()
endif()
