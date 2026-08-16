#include "addition/config.hpp"
#include "addition/p2p.hpp"
#include "addition/rpc_access.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
    if (!addition::is_public_read_command("getinfo") ||
        !addition::is_public_read_command("monetary_info") ||
        !addition::is_public_read_command("crypto_selftest") ||
        !addition::is_public_read_command("tx_status") ||
        !addition::is_public_read_command("peers") ||
        !addition::is_public_read_command("getblock") ||
        !addition::is_public_read_command("getblockhash") ||
        !addition::is_public_read_command("getblockraw")) {
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
        "privacy_note_prepare",
        "privacy_mint_open",
        "privacy_spend_open",
        "privacy_mint_zk",
        "privacy_spend_zk",
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

    {
        std::string method;
        std::vector<std::string> params;
        std::string id_json;
        std::string err;
        const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getinfo\",\"params\":[]}";
        if (!addition::parse_jsonrpc_request(body, method, params, id_json, err) ||
            method != "getinfo" || id_json != "1") {
            std::cerr << "test failed: parse_jsonrpc_request getinfo: " << err << '\n';
            return 1;
        }
        const std::string body2 = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"getblockraw\",\"params\":[\"0\"]}";
        if (!addition::parse_jsonrpc_request(body2, method, params, id_json, err) ||
            method != "getblockraw" || params.size() != 1 || params[0] != "0") {
            std::cerr << "test failed: parse_jsonrpc_request getblockraw: " << err << '\n';
            return 1;
        }
        const auto encoded = addition::jsonrpc_result_body("1", "network=testnet");
        if (encoded.find("\"jsonrpc\":\"2.0\"") == std::string::npos ||
            encoded.find("network=testnet") == std::string::npos) {
            std::cerr << "test failed: jsonrpc_result_body\n";
            return 1;
        }
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

    if (!addition::is_loopback_host("127.0.0.1") ||
        !addition::is_loopback_endpoint("127.0.0.1:28546") ||
        addition::is_external_advertised_peer("127.0.0.1:28546") ||
        addition::is_external_advertised_peer("self") ||
        addition::is_external_advertised_peer("probe-self") ||
        addition::is_external_advertised_peer("n-deadbeef") ||
        !addition::is_self_peer_label("probe-self") ||
        !addition::is_external_advertised_peer("34.27.30.115:28545")) {
        std::cerr << "test failed: loopback/self must not count as external peers\n";
        return 1;
    }

    {
        addition::PeerNetwork net;
        net.add_peer("self");
        net.add_peer("probe-self");
        net.add_peer("127.0.0.1:28546");
        net.add_peer("n-abc");
        net.add_peer("34.27.30.115:28545");
        if (net.peer_count() != 5) {
            std::cerr << "test failed: internal peer set must keep loopback/self for sync\n";
            return 1;
        }
        if (net.advertised_peer_count() != 1 || net.loopback_peer_count() != 1) {
            std::cerr << "test failed: advertised/local peer split\n";
            return 1;
        }
        const auto remote = net.advertised_peers();
        if (remote.size() != 1 || remote[0] != "34.27.30.115:28545") {
            std::cerr << "test failed: advertised peer list\n";
            return 1;
        }
    }

    std::cout << "all rpc access tests passed\n";
    return 0;
}
