#pragma once

#include <string>

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
std::string dispatch_public_read_rpc(RpcServer& rpc, const std::string& raw);

} // namespace addition
