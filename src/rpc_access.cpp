#include "addition/rpc_access.hpp"

#include "addition/rpc_server.hpp"

#include <cstdio>
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
           eq_cmd(cmd, "getblockhash") ||
           eq_cmd(cmd, "getblockraw");
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
           eq_cmd(cmd, "nft_info") ||
           eq_cmd(cmd, "swap_tvl") ||
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

std::string public_rpc_banner_text(const std::string& network_mode) {
    return std::string("addition-public-rpc read-only ") + network_mode;
}

std::string http_rpc_response(int status, const std::string& body) {
    return http_rpc_response(status, body, "text/plain; charset=utf-8");
}

std::string http_rpc_response(int status, const std::string& body, const std::string& content_type) {
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

    // Wildcard CORS is for the credential-less public read allowlist only
    // (curl /rpc?cmd=getinfo). Writes stay 403. Methods are GET/OPTIONS so a
    // browser cannot use this port as a generic POST wallet endpoint.
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return out.str();
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

std::string jsonrpc_result_body(const std::string& id_json, const std::string& result_text) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + id_json +
           ",\"result\":\"" + json_escape(result_text) + "\"}";
}

std::string jsonrpc_error_body(const std::string& id_json, int code, const std::string& message) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + id_json +
           ",\"error\":{\"code\":" + std::to_string(code) +
           ",\"message\":\"" + json_escape(message) + "\"}}";
}

namespace {

void skip_ws(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        ++i;
    }
}

bool parse_json_string(const std::string& s, std::size_t& i, std::string& out) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (i >= s.size()) {
                return false;
            }
            const char e = s[i++];
            if (e == '"' || e == '\\' || e == '/') {
                out.push_back(e);
            } else if (e == 'n') {
                out.push_back('\n');
            } else if (e == 'r') {
                out.push_back('\r');
            } else if (e == 't') {
                out.push_back('\t');
            } else {
                return false;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool parse_json_number_token(const std::string& s, std::size_t& i, std::string& out) {
    skip_ws(s, i);
    if (i >= s.size()) {
        return false;
    }
    const std::size_t start = i;
    if (s[i] == '-') {
        ++i;
    }
    if (i >= s.size() || s[i] < '0' || s[i] > '9') {
        return false;
    }
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    out = s.substr(start, i - start);
    return true;
}

} // namespace

bool parse_jsonrpc_request(const std::string& body,
                           std::string& method,
                           std::vector<std::string>& params,
                           std::string& id_json,
                           std::string& error) {
    method.clear();
    params.clear();
    id_json = "null";
    error.clear();

    std::size_t i = 0;
    skip_ws(body, i);
    if (i >= body.size() || body[i] != '{') {
        error = "JSON-RPC body must be an object";
        return false;
    }
    ++i;

    std::string jsonrpc;
    bool have_method = false;
    while (i < body.size()) {
        skip_ws(body, i);
        if (i < body.size() && body[i] == '}') {
            break;
        }
        std::string key;
        if (!parse_json_string(body, i, key)) {
            error = "invalid JSON key";
            return false;
        }
        skip_ws(body, i);
        if (i >= body.size() || body[i] != ':') {
            error = "expected ':'";
            return false;
        }
        ++i;
        skip_ws(body, i);
        if (i >= body.size()) {
            error = "truncated JSON-RPC object";
            return false;
        }

        if (key == "jsonrpc") {
            if (!parse_json_string(body, i, jsonrpc)) {
                error = "jsonrpc must be a string";
                return false;
            }
        } else if (key == "method") {
            if (!parse_json_string(body, i, method)) {
                error = "method must be a string";
                return false;
            }
            have_method = true;
        } else if (key == "id") {
            if (body[i] == '"') {
                std::string id_str;
                if (!parse_json_string(body, i, id_str)) {
                    error = "invalid id string";
                    return false;
                }
                id_json = std::string("\"") + json_escape(id_str) + "\"";
            } else if (body.compare(i, 4, "null") == 0) {
                i += 4;
                id_json = "null";
            } else {
                std::string num;
                if (!parse_json_number_token(body, i, num)) {
                    error = "id must be string, number, or null";
                    return false;
                }
                id_json = num;
            }
        } else if (key == "params") {
            if (body[i] != '[') {
                error = "params must be a positional array";
                return false;
            }
            ++i;
            skip_ws(body, i);
            while (i < body.size() && body[i] != ']') {
                skip_ws(body, i);
                if (i >= body.size()) {
                    error = "truncated params";
                    return false;
                }
                std::string value;
                if (body[i] == '"') {
                    if (!parse_json_string(body, i, value)) {
                        error = "invalid params string";
                        return false;
                    }
                } else {
                    if (!parse_json_number_token(body, i, value)) {
                        error = "params entries must be strings or integers";
                        return false;
                    }
                }
                params.push_back(value);
                skip_ws(body, i);
                if (i < body.size() && body[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < body.size() && body[i] == ']') {
                    break;
                }
                error = "invalid params array";
                return false;
            }
            if (i >= body.size() || body[i] != ']') {
                error = "unterminated params array";
                return false;
            }
            ++i;
        } else {
            error = "unknown JSON-RPC field";
            return false;
        }
        skip_ws(body, i);
        if (i < body.size() && body[i] == ',') {
            ++i;
            continue;
        }
        if (i < body.size() && body[i] == '}') {
            break;
        }
        error = "invalid JSON-RPC object";
        return false;
    }

    if (jsonrpc != "2.0") {
        error = "jsonrpc must be 2.0";
        return false;
    }
    if (!have_method || method.empty()) {
        error = "method must be a string";
        return false;
    }
    return true;
}

namespace {

std::string join_text_command(const std::string& method, const std::vector<std::string>& params) {
    std::string cmd = method;
    for (const auto& p : params) {
        cmd.push_back(' ');
        cmd += p;
    }
    return cmd;
}

std::string dispatch_public_jsonrpc(RpcServer& rpc,
                                    const std::string& method,
                                    const std::vector<std::string>& params,
                                    const std::string& id_json) {
    if (method.find(' ') != std::string::npos || method.find('\n') != std::string::npos) {
        return jsonrpc_error_body(id_json, -32600, "method must be a single TEXT RPC command name");
    }
    if (!is_public_read_command(method)) {
        return jsonrpc_error_body(id_json, -32601, "error: command disabled on public RPC");
    }
    for (const auto& p : params) {
        if (p.find(' ') != std::string::npos || p.find('\n') != std::string::npos || p.find('\r') != std::string::npos) {
            return jsonrpc_error_body(id_json, -32602, "params must be single TEXT RPC tokens");
        }
    }
    const auto reply = rpc.handle_command(join_text_command(method, params), false);
    if (reply.rfind("error:", 0) == 0) {
        return jsonrpc_error_body(id_json, -32000, reply);
    }
    return jsonrpc_result_body(id_json, reply);
}

std::vector<std::string> split_query_params(const std::string& raw) {
    std::vector<std::string> out;
    if (raw.empty()) {
        return out;
    }
    std::size_t start = 0;
    while (start <= raw.size()) {
        const auto comma = raw.find(',', start);
        const auto part = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!part.empty()) {
            out.push_back(part);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

} // namespace

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
            return http_rpc_response(200, rpc.public_rpc_banner());
        }
        if (path == "/jsonrpc" || path == "/jsonrpc/") {
            if (method == "HEAD") {
                return http_rpc_response(200, "", "application/json");
            }
            std::string jmethod;
            std::vector<std::string> jparams;
            std::string id_json = "1";
            if (method == "GET") {
                jmethod = query_param(query, "method");
                jparams = split_query_params(query_param(query, "params"));
                const auto idq = query_param(query, "id");
                if (!idq.empty()) {
                    id_json = idq;
                    bool numeric = !id_json.empty();
                    for (char c : id_json) {
                        if (c < '0' || c > '9') {
                            numeric = false;
                            break;
                        }
                    }
                    if (!numeric) {
                        id_json = std::string("\"") + json_escape(idq) + "\"";
                    }
                }
                if (jmethod.empty()) {
                    const auto body = jsonrpc_error_body("null", -32600, "missing method");
                    return http_rpc_response(200, body, "application/json");
                }
                const auto body = dispatch_public_jsonrpc(rpc, jmethod, jparams, id_json);
                return http_rpc_response(200, body, "application/json");
            }
            if (method == "POST") {
                std::string parsed_method;
                std::vector<std::string> parsed_params;
                std::string parsed_id;
                std::string perr;
                if (!parse_jsonrpc_request(http_body(raw), parsed_method, parsed_params, parsed_id, perr)) {
                    const auto body = jsonrpc_error_body(parsed_id.empty() ? "null" : parsed_id, -32600, perr);
                    return http_rpc_response(200, body, "application/json");
                }
                const auto body = dispatch_public_jsonrpc(rpc, parsed_method, parsed_params, parsed_id);
                return http_rpc_response(200, body, "application/json");
            }
            return http_rpc_response(405, "error: method not allowed");
        }
        std::string cmd;
        std::string error;
        if (!parse_http_rpc_command(raw, cmd, error)) {
            if (error == "not an RPC path") {
                return http_rpc_response(404, "error: use /rpc?cmd=getinfo or /jsonrpc?method=getinfo");
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
