#include "addition/rpc_access.hpp"

#include "addition/rpc_server.hpp"

#include <sstream>

namespace addition {
namespace {

bool eq_cmd(const std::string& cmd, const char* name) {
    return cmd == name;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+' ) {
            out.push_back(' ');
            continue;
        }
        if (in[i] == '%' && i + 2 < in.size()) {
            const int hi = hex_value(in[i + 1]);
            const int lo = hex_value(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string query_param(const std::string& query, const std::string& key) {
    std::size_t start = 0;
    while (start < query.size()) {
        const auto amp = query.find('&', start);
        const auto part = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        const auto eq = part.find('=');
        const auto k = eq == std::string::npos ? part : part.substr(0, eq);
        if (url_decode(k) == key) {
            return eq == std::string::npos ? std::string() : url_decode(part.substr(eq + 1));
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
    return {};
}

std::string request_path_query(const std::string& raw, std::string& method, std::string& path, std::string& query) {
    method.clear();
    path.clear();
    query.clear();
    std::istringstream iss(raw);
    std::string target;
    std::string version;
    if (!(iss >> method >> target >> version)) {
        return "malformed request line";
    }
    const auto qpos = target.find('?');
    if (qpos == std::string::npos) {
        path = target;
    } else {
        path = target.substr(0, qpos);
        query = target.substr(qpos + 1);
    }
    return {};
}

std::string http_body(const std::string& raw) {
    const auto pos = raw.find("\r\n\r\n");
    if (pos != std::string::npos) {
        return raw.substr(pos + 4);
    }
    const auto pos2 = raw.find("\n\n");
    if (pos2 != std::string::npos) {
        return raw.substr(pos2 + 2);
    }
    return {};
}

} // namespace

bool is_public_read_command(const std::string& cmd) {
    return eq_cmd(cmd, "getinfo") ||
           eq_cmd(cmd, "monetary_info") ||
           eq_cmd(cmd, "crypto_selftest") ||
           eq_cmd(cmd, "tx_status") ||
           eq_cmd(cmd, "peers") ||
           eq_cmd(cmd, "getblock") ||
           eq_cmd(cmd, "getblockhash");
}

bool is_remote_allowed_command(const std::string& cmd) {
    if (is_public_read_command(cmd)) {
        return true;
    }
    return eq_cmd(cmd, "protocol_status") ||
           eq_cmd(cmd, "fee_info") ||
           eq_cmd(cmd, "node_pubkey") ||
           eq_cmd(cmd, "getbalance") ||
           eq_cmd(cmd, "getbalance_instant") ||
           eq_cmd(cmd, "staked") ||
           eq_cmd(cmd, "stake_claimable") ||
           eq_cmd(cmd, "token_balance") ||
           eq_cmd(cmd, "token_info") ||
           eq_cmd(cmd, "swap_quote") ||
           eq_cmd(cmd, "swap_pool_info") ||
           eq_cmd(cmd, "swap_quote_route") ||
           eq_cmd(cmd, "swap_best_route") ||
           eq_cmd(cmd, "nft_owner") ||
           eq_cmd(cmd, "privacy_status") ||
           eq_cmd(cmd, "bridge_balance") ||
           eq_cmd(cmd, "bridge_attestor") ||
           eq_cmd(cmd, "pouw_storage_deal_status") ||
           eq_cmd(cmd, "pouw_storage_worker_status") ||
           eq_cmd(cmd, "pouw_compute_job_status") ||
           eq_cmd(cmd, "pouw_compute_worker_status") ||
           eq_cmd(cmd, "pm_inbox") ||
           eq_cmd(cmd, "pm_status") ||
           eq_cmd(cmd, "pm_fetch") ||
           eq_cmd(cmd, "verify_message") ||
           eq_cmd(cmd, "ai_status");
}

std::string first_command_token(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    return cmd;
}

bool is_http_rpc_request(const std::string& raw) {
    return raw.rfind("GET ", 0) == 0 ||
           raw.rfind("POST ", 0) == 0 ||
           raw.rfind("OPTIONS ", 0) == 0 ||
           raw.rfind("HEAD ", 0) == 0;
}

bool parse_http_rpc_command(const std::string& raw, std::string& cmd, std::string& error) {
    cmd.clear();
    error.clear();
    std::string method;
    std::string path;
    std::string query;
    const auto line_err = request_path_query(raw, method, path, query);
    if (!line_err.empty()) {
        error = line_err;
        return false;
    }
    if (method == "OPTIONS" || method == "HEAD") {
        return true;
    }
    if (path != "/rpc" && path != "/rpc/") {
        error = "not an RPC path";
        return false;
    }
    cmd = query_param(query, "cmd");
    if (cmd.empty() && method == "POST") {
        cmd = http_body(raw);
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) {
            cmd.pop_back();
        }
    }
    if (cmd.empty()) {
        error = "missing cmd";
        return false;
    }
    return true;
}

std::string http_rpc_response(int status, const std::string& body) {
    const char* reason = "OK";
    switch (status) {
    case 200:
        reason = "OK";
        break;
    case 204:
        reason = "No Content";
        break;
    case 400:
        reason = "Bad Request";
        break;
    case 403:
        reason = "Forbidden";
        break;
    case 404:
        reason = "Not Found";
        break;
    case 405:
        reason = "Method Not Allowed";
        break;
    default:
        reason = "Error";
        break;
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
        << "Content-Type: text/plain; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return out.str();
}

std::string dispatch_public_read_rpc(RpcServer& rpc, const std::string& raw) {
    if (is_http_rpc_request(raw)) {
        std::string method;
        std::string path;
        std::string query;
        const auto line_err = request_path_query(raw, method, path, query);
        if (!line_err.empty()) {
            return http_rpc_response(400, "error: malformed HTTP request");
        }
        if (method == "OPTIONS") {
            return http_rpc_response(204, "");
        }
        if (path == "/" || path == "/health") {
            return http_rpc_response(200, "addition-public-rpc read-only testnet");
        }
        std::string cmd;
        std::string error;
        if (!parse_http_rpc_command(raw, cmd, error)) {
            if (error == "not an RPC path") {
                return http_rpc_response(404, "error: use /rpc?cmd=getinfo");
            }
            return http_rpc_response(400, std::string("error: ") + error);
        }
        if (method == "HEAD") {
            return http_rpc_response(200, "");
        }
        const auto token = first_command_token(cmd);
        if (!is_public_read_command(token)) {
            return http_rpc_response(403, "error: command disabled on public RPC");
        }
        return http_rpc_response(200, rpc.handle_command(cmd, false));
    }

    const auto token = first_command_token(raw);
    if (!is_public_read_command(token)) {
        return "error: command disabled on public RPC";
    }
    return rpc.handle_command(raw, false);
}

} // namespace addition
