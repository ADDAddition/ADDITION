#pragma once

#include <string>
#include <vector>

namespace addition {

class RpcServer;

// Trusted/local RPC (127.0.0.1) may run any command.
// LAN RPC uses the remote allowlist and still requires a token.
// Public read RPC uses the tighter public allowlist and never runs writes.

bool is_public_read_command(const std::string& cmd);
bool is_remote_allowed_command(const std::string& cmd);

std::string first_command_token(const std::string& line);
bool is_http_rpc_request(const std::string& raw);
bool parse_http_rpc_command(const std::string& raw, std::string& cmd, std::string& error);
std::string http_rpc_response(int status, const std::string& body);
std::string http_rpc_response(int status, const std::string& body, const std::string& content_type);
std::string public_rpc_banner_text(const std::string& network_mode);
std::string dispatch_public_read_rpc(RpcServer& rpc, const std::string& raw);

// Public-read JSON-RPC 2.0 (same allowlist as public TEXT RPC). No writes.
bool parse_jsonrpc_request(const std::string& body,
                           std::string& method,
                           std::vector<std::string>& params,
                           std::string& id_json,
                           std::string& error);
std::string json_escape(const std::string& in);
std::string jsonrpc_result_body(const std::string& id_json, const std::string& result_text);
std::string jsonrpc_error_body(const std::string& id_json, int code, const std::string& message);

} // namespace addition
