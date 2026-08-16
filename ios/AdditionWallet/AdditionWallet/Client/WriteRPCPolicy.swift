import Foundation

enum WriteRPCPolicy {
    static let defaultHost = "127.0.0.1"
    static let defaultPort = 8545
    static let maxRPCLine = 32768
    static let contact = "contact@additionblockchain.com"

    static let publicReadCommands: Set<String> = [
        "getinfo",
        "getblock",
        "getblockraw",
    ]

    static let writeCommands: Set<String> = [
        "createwallet",
        "wallet_list",
        "wallet_info",
        "wallet_balance",
        "wallet_send",
        "getbalance",
        "fee_info",
        "getinfo",
    ]

    static let insecureCommands: Set<String> = [
        "sendtx",
        "sendtx_hash",
        "sign_message",
    ]

    static let foreignChainTokens: Set<String> = [
        "bitcoin",
        "btc",
        "ethereum",
        "eth",
        "solana",
        "sol",
        "metamask",
        "walletconnect",
    ]

    static let loopbackHosts: Set<String> = [
        "127.0.0.1",
        "::1",
        "localhost",
        "localhost.localdomain",
    ]

    static let refusedPublicWriteHosts: Set<String> = [
        "rpc.additionblockchain.com",
        "additionblockchain.com",
        "www.additionblockchain.com",
        "34.27.30.115",
    ]

    static let knownPublicReadURLs: Set<String> = [
        "https://rpc.additionblockchain.com/rpc",
        "http://34.27.30.115/rpc",
        "http://34.27.30.115:38545/rpc",
    ]

    static func host(from endpoint: String) throws -> String {
        let text = endpoint.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty {
            throw AdditionError.writeRPCRefused
        }
        if !text.contains("://") {
            if text.contains(":") && !text.hasPrefix("[") && text.split(separator: ":").count == 2 {
                return String(text.split(separator: ":", maxSplits: 1)[0]).lowercased()
            }
            if text.hasPrefix("["), let end = text.firstIndex(of: "]") {
                return String(text[text.index(after: text.startIndex)..<end]).lowercased()
            }
            return text.lowercased()
        }
        guard let url = URL(string: text), let host = url.host, !host.isEmpty else {
            throw AdditionError.writeRPCRefused
        }
        return host.lowercased()
    }

    static func classify(host raw: String) -> WriteHostClass {
        let host = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if host.isEmpty {
            return .refusedPublic
        }
        if isRefusedPublicWrite(host) {
            return .refusedPublic
        }
        if isLoopback(host) {
            return .loopback
        }
        if isLAN(host) {
            return .lan
        }
        return .refusedPublic
    }

    static func assertWriteEndpoint(_ endpoint: String) throws -> String {
        let host = try host(from: endpoint)
        if classify(host: host) == .refusedPublic {
            throw AdditionError.writeRPCRefused
        }
        return host
    }

    static func isKnownPublicReadEndpoint(_ endpoint: String) -> Bool {
        let trimmed = endpoint.trimmingCharacters(in: .whitespacesAndNewlines).trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        if knownPublicReadURLs.contains(trimmed) || knownPublicReadURLs.contains(endpoint.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return true
        }
        guard let host = try? host(from: endpoint) else {
            return false
        }
        return host == "rpc.additionblockchain.com" || host == "34.27.30.115"
    }

    static func assertCommand(_ command: String, write: Bool) throws -> String {
        if command.contains("\n") || command.contains("\r") {
            throw AdditionError.rpc("RPC command must be a single line")
        }
        let token = KVParser.firstToken(command)
        if token.isEmpty {
            throw AdditionError.rpc("empty RPC command")
        }
        let lowered = token.lowercased()
        if insecureCommands.contains(lowered) {
            throw AdditionError.insecureCommand(token)
        }
        let parts = Set(lowered.split(separator: "_").map(String.init))
        if foreignChainTokens.contains(lowered)
            || !parts.isDisjoint(with: foreignChainTokens)
            || foreignChainTokens.contains(where: { lowered.contains($0) }) {
            throw AdditionError.additionOnly
        }
        if write {
            if !writeCommands.contains(lowered) {
                throw AdditionError.commandNotAllowed(token)
            }
        } else if !publicReadCommands.contains(lowered) {
            throw AdditionError.commandNotAllowed(token)
        }
        if command.count > maxRPCLine {
            throw AdditionError.rpc("RPC command exceeds 32768-byte TEXT RPC limit")
        }
        return token
    }

    static func parseConfirmedBalance(_ line: String) throws -> UInt64 {
        let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty || text == "RPC offline" {
            throw AdditionError.rpcOffline
        }
        if text.hasPrefix("error:") {
            throw AdditionError.rpc(text)
        }
        let values = KVParser.parse(text)
        if let raw = values["confirmed"] {
            guard let value = UInt64(raw), raw.allSatisfy(\.isNumber) else {
                throw AdditionError.rpc("RPC error: confirmed balance is not a whole unit")
            }
            return value
        }
        if text.allSatisfy(\.isNumber), let value = UInt64(text) {
            return value
        }
        throw AdditionError.rpc("RPC error: balance reply is not a whole-unit amount")
    }

    static func parseGetinfo(_ line: String) throws -> [String: String] {
        let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty || text == "RPC offline" {
            throw AdditionError.rpcOffline
        }
        if text.hasPrefix("error:") {
            throw AdditionError.rpc(text)
        }
        let values = KVParser.parse(text)
        guard values["network"] != nil else {
            throw AdditionError.rpc("RPC error: getinfo missing network")
        }
        if let height = values["height"], !height.allSatisfy(\.isNumber) {
            throw AdditionError.rpc("RPC error: getinfo height is not an integer")
        }
        return values
    }

    private static func isRefusedPublicWrite(_ host: String) -> Bool {
        if refusedPublicWriteHosts.contains(host) {
            return true
        }
        if host.hasSuffix(".additionblockchain.com") {
            return true
        }
        return false
    }

    private static func isLoopback(_ host: String) -> Bool {
        if loopbackHosts.contains(host) {
            return true
        }
        return IPv4.isLoopback(host)
    }

    private static func isLAN(_ host: String) -> Bool {
        if host.hasSuffix(".local") {
            return true
        }
        return IPv4.isPrivateOrLinkLocal(host)
    }
}

private enum IPv4 {
    static func isLoopback(_ host: String) -> Bool {
        if host == "::1" {
            return true
        }
        guard let parts = octets(host) else { return false }
        return parts[0] == 127
    }

    static func isPrivateOrLinkLocal(_ host: String) -> Bool {
        guard let parts = octets(host) else { return false }
        if parts[0] == 10 {
            return true
        }
        if parts[0] == 192 && parts[1] == 168 {
            return true
        }
        if parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31 {
            return true
        }
        if parts[0] == 169 && parts[1] == 254 {
            return true
        }
        return false
    }

    static func octets(_ host: String) -> [Int]? {
        let parts = host.split(separator: ".")
        guard parts.count == 4 else { return nil }
        let numbers = parts.compactMap { Int($0) }
        guard numbers.count == 4, numbers.allSatisfy({ (0...255).contains($0) }) else {
            return nil
        }
        return numbers
    }
}

struct WriteEndpoint: Equatable {
    var host: String
    var port: Int
    var token: String
    var scheme: String

    static func parse(_ raw: String, token: String = "") throws -> WriteEndpoint {
        var text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty {
            text = "\(WriteRPCPolicy.defaultHost):\(WriteRPCPolicy.defaultPort)"
        }
        var scheme = "text"
        var host = WriteRPCPolicy.defaultHost
        var port = WriteRPCPolicy.defaultPort
        if text.contains("://"), let url = URL(string: text) {
            switch url.scheme {
            case "http", "https":
                scheme = url.scheme ?? "http"
            case "text":
                scheme = "text"
            default:
                throw AdditionError.writeRPCRefused
            }
            guard let parsedHost = url.host, !parsedHost.isEmpty else {
                throw AdditionError.writeRPCRefused
            }
            host = parsedHost.lowercased()
            if let parsedPort = url.port {
                port = parsedPort
            } else if scheme == "https" {
                port = 443
            } else if scheme == "http" {
                port = 80
            }
        } else if text.hasPrefix("["), let end = text.firstIndex(of: "]") {
            host = String(text[text.index(after: text.startIndex)..<end]).lowercased()
            let rest = text[text.index(after: end)...]
            if rest.hasPrefix(":"), let parsed = Int(rest.dropFirst()) {
                port = parsed
            }
        } else if text.split(separator: ":").count == 2 {
            let parts = text.split(separator: ":", maxSplits: 1)
            host = String(parts[0]).lowercased()
            guard let parsed = Int(parts[1]) else {
                throw AdditionError.writeRPCRefused
            }
            port = parsed
        } else {
            host = text.lowercased()
        }
        _ = try WriteRPCPolicy.assertWriteEndpoint(host)
        if port <= 0 || port > 65535 {
            throw AdditionError.writeRPCRefused
        }
        return WriteEndpoint(host: host, port: port, token: token, scheme: scheme)
    }

    var display: String {
        if scheme == "http" || scheme == "https" {
            return "\(scheme)://\(host):\(port)"
        }
        return "\(host):\(port)"
    }
}
