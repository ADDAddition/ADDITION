#include "addition/config.hpp"
#include "addition/rpc_access.hpp"

#include <iostream>
#include <string>

int main() {
    if (!addition::is_public_read_command("getinfo") ||
        !addition::is_public_read_command("monetary_info") ||
        !addition::is_public_read_command("crypto_selftest") ||
        !addition::is_public_read_command("tx_status") ||
        !addition::is_public_read_command("peers") ||
        !addition::is_public_read_command("getblock") ||
        !addition::is_public_read_command("getblockhash")) {
        std::cerr << "test failed: public read allowlist missing a required command\n";
        return 1;
    }

    const char* blocked[] = {
        "mine",
        "sendtx",
        "sendtx_signed",
        "createwallet",
        "wallet_list",
        "wallet_info",
        "wallet_balance",
        "wallet_send",
        "wallet_sign",
        "identity_rotate_propose",
        "identity_rotate_commit",
        "stake_policy",
        "contract_deploy",
        "token_create",
        "swap_exact_in",
        "addpeer",
        "peer_inbound",
        "pm_inbox",
        "getbalance",
    };
    for (const char* cmd : blocked) {
        if (addition::is_public_read_command(cmd)) {
            std::cerr << "test failed: public RPC must not allow " << cmd << '\n';
            return 1;
        }
    }

    if (addition::first_command_token("getblock 0 extra") != "getblock") {
        std::cerr << "test failed: first_command_token\n";
        return 1;
    }

    if (!addition::is_http_rpc_request("GET /rpc?cmd=getinfo HTTP/1.1\r\n")) {
        std::cerr << "test failed: HTTP GET not detected\n";
        return 1;
    }
    if (addition::is_http_rpc_request("getinfo")) {
        std::cerr << "test failed: TCP line misclassified as HTTP\n";
        return 1;
    }

    std::string cmd;
    std::string error;
    if (!addition::parse_http_rpc_command("GET /rpc?cmd=getinfo HTTP/1.1\r\n\r\n", cmd, error) ||
        cmd != "getinfo") {
        std::cerr << "test failed: parse GET cmd: " << error << '\n';
        return 1;
    }
    if (!addition::parse_http_rpc_command("GET /rpc?cmd=tx_status%20abcd HTTP/1.1\r\n\r\n", cmd, error) ||
        cmd != "tx_status abcd") {
        std::cerr << "test failed: parse URL-encoded cmd: [" << cmd << "] " << error << '\n';
        return 1;
    }
    if (!addition::parse_http_rpc_command("POST /rpc HTTP/1.1\r\n\r\ngetblock 0\n", cmd, error) ||
        cmd != "getblock 0") {
        std::cerr << "test failed: parse POST body: [" << cmd << "] " << error << '\n';
        return 1;
    }

    const auto resp = addition::http_rpc_response(200, "network=testnet");
    if (resp.find("HTTP/1.1 200 OK") == std::string::npos ||
        resp.find("network=testnet") == std::string::npos ||
        resp.find("Access-Control-Allow-Origin: *") == std::string::npos ||
        resp.find("Cache-Control: no-store") == std::string::npos) {
        std::cerr << "test failed: HTTP response format\n";
        return 1;
    }

    const auto preflight = addition::http_rpc_response(204, "");
    if (preflight.find("HTTP/1.1 204 No Content") == std::string::npos ||
        preflight.find("Access-Control-Allow-Origin: *") == std::string::npos ||
        preflight.find("Cache-Control: no-store") == std::string::npos) {
        std::cerr << "test failed: OPTIONS/CORS response format\n";
        return 1;
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--network";
        char arg2[] = "testnet";
        char arg3[] = "--public-rpc";
        char arg4[] = "--public-rpc-port";
        char arg5[] = "38545";
        char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
        if (!addition::apply_cli_args(6, argv, ncfg, help, err)) {
            std::cerr << "test failed: cli public-rpc parse: " << err << '\n';
            return 1;
        }
        if (!ncfg.enable_public_rpc || ncfg.public_rpc_port != 38545) {
            std::cerr << "test failed: --public-rpc not applied\n";
            return 1;
        }
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--network";
        char arg2[] = "testnet";
        char arg3[] = "--public-rpc";
        char arg4[] = "--public-rpc-bind";
        char arg5[] = "127.0.0.1";
        char arg6[] = "--local-rpc-port";
        char arg7[] = "8546";
        char arg8[] = "--p2p-port";
        char arg9[] = "28546";
        char arg10[] = "--bootstrap";
        char arg11[] = "127.0.0.1:28545";
        char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11};
        if (!addition::apply_cli_args(12, argv, ncfg, help, err)) {
            std::cerr << "test failed: cli two-node parse: " << err << '\n';
            return 1;
        }
        if (!ncfg.enable_public_rpc ||
            ncfg.public_rpc_bind != "127.0.0.1" ||
            ncfg.local_rpc_port != 8546 ||
            ncfg.p2p_port != 28546 ||
            ncfg.bootstrap_peers.size() != 1 ||
            ncfg.bootstrap_peers[0] != "127.0.0.1:28545") {
            std::cerr << "test failed: two-node CLI flags not applied\n";
            return 1;
        }
    }

    std::cout << "all rpc access tests passed\n";
    return 0;
}
