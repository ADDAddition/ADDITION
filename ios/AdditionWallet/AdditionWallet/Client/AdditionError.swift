import Foundation

enum AdditionError: LocalizedError, Equatable {
    case rpcOffline
    case rpc(String)
    case invalidAmount
    case invalidAddress
    case invalidWalletName
    case writeRPCRefused
    case publicReadRefused
    case insecureCommand(String)
    case additionOnly
    case commandNotAllowed(String)
    case missingField(String)
    case sameAddress

    var errorDescription: String? {
        switch self {
        case .rpcOffline:
            return "RPC offline"
        case .rpc(let line):
            return line
        case .invalidAmount:
            return "whole-unit amounts only; no decimal subunit"
        case .invalidAddress:
            return "ADDITION address must be 128 hex characters"
        case .invalidWalletName:
            return "invalid wallet name (use 1-64 letters, digits, _ or -)"
        case .writeRPCRefused:
            return "write RPC refused: not a loopback or LAN node you control"
        case .publicReadRefused:
            return "unknown public read endpoint"
        case .insecureCommand(let name):
            return "refusing insecure RPC command: \(name)"
        case .additionOnly:
            return "ADDITION RPC only"
        case .commandNotAllowed(let name):
            return "command not allowed: \(name)"
        case .missingField(let name):
            return "RPC error: missing \(name)"
        case .sameAddress:
            return "refusing to send to the same address"
        }
    }
}

func additionErrorUnhandled(_ value: Never) -> Never {
    switch value {}
}

enum WriteHostClass: String, Equatable {
    case loopback
    case lan
    case refusedPublic = "refused_public"
}
