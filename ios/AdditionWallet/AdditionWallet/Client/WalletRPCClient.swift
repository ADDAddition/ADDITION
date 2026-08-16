import Foundation

struct WalletRecord: Equatable {
    var name: String
    var address: String
    var algorithm: String
    var publicKey: String
}

struct WalletRPCClient {
    var rpc: TextRPCClient

    func createwallet(name: String = "default") throws -> WalletRecord {
        let line = try rpc.call(WalletCommands.createwallet(name: name))
        if line.hasPrefix("error:") {
            throw AdditionError.rpc(line)
        }
        let values = KVParser.parse(line)
        guard let address = values["address"] else {
            throw AdditionError.missingField("address")
        }
        return WalletRecord(
            name: values["name"] ?? name,
            address: try Address.validate(address),
            algorithm: values["algo"] ?? "",
            publicKey: values["pub"] ?? ""
        )
    }

    func walletInfo(name: String) throws -> WalletRecord {
        let line = try rpc.call(try WalletCommands.walletInfo(name: name))
        if line.hasPrefix("error:") {
            throw AdditionError.rpc(line)
        }
        let values = KVParser.parse(line)
        guard let address = values["address"] else {
            throw AdditionError.missingField("address")
        }
        if let pub = values["pub"], !pub.isEmpty, let derived = try? Address.derive(pubkeyHex: pub) {
            if derived != address.lowercased() {
                throw AdditionError.rpc("RPC error: address does not match public key")
            }
        }
        return WalletRecord(
            name: values["name"] ?? name,
            address: try Address.validate(address),
            algorithm: values["algo"] ?? "",
            publicKey: values["pub"] ?? ""
        )
    }

    func walletList() throws -> String {
        let line = try rpc.call(WalletCommands.walletList())
        if line.hasPrefix("error:") {
            throw AdditionError.rpc(line)
        }
        return line
    }

    func balance(name: String) throws -> UInt64 {
        let line = try rpc.call(try WalletCommands.walletBalance(name: name))
        return try WriteRPCPolicy.parseConfirmedBalance(line)
    }

    func getbalance(address: String) throws -> UInt64 {
        let line = try rpc.call(try WalletCommands.getbalance(address: address))
        return try WriteRPCPolicy.parseConfirmedBalance(line)
    }

    func send(name: String, to: String, amount: UInt64, fee: UInt64?) throws -> String {
        let info = try walletInfo(name: name)
        if to == info.address {
            throw AdditionError.sameAddress
        }
        let line = try rpc.call(try WalletCommands.walletSend(name: name, to: to, amount: amount, fee: fee))
        if line.hasPrefix("error:") {
            throw AdditionError.rpc(line)
        }
        if !line.contains("ok:gossiped") && !line.contains("hash=") {
            throw AdditionError.rpc("RPC error: wallet_send did not confirm")
        }
        return line
    }

    func getinfo() throws -> [String: String] {
        try WriteRPCPolicy.parseGetinfo(try rpc.call(WalletCommands.getinfo()))
    }

    func feeInfo() throws -> [String: String] {
        let line = try rpc.call(WalletCommands.feeInfo())
        if line.hasPrefix("error:") {
            throw AdditionError.rpc(line)
        }
        return KVParser.parse(line)
    }
}
