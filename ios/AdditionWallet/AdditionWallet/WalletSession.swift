import Foundation
import SwiftUI

@MainActor
final class WalletSession: ObservableObject {
    @Published var walletName: String
    @Published var writeEndpointText: String
    @Published var rpcToken: String
    @Published var address: String = ""
    @Published var algorithm: String = ""
    @Published var balanceText: String = ""
    @Published var balanceReady: Bool = false
    @Published var nodeLine: String = ""
    @Published var publicReadLine: String = ""
    @Published var status: String = "Set a write RPC node you control, then create or load a wallet."
    @Published var lastSend: String = ""
    @Published var busy: Bool = false

    private let defaults = UserDefaults.standard

    init() {
        walletName = defaults.string(forKey: "walletName") ?? "default"
        writeEndpointText = defaults.string(forKey: "writeEndpoint") ?? "127.0.0.1:8545"
        rpcToken = defaults.string(forKey: "rpcToken") ?? ""
        address = defaults.string(forKey: "address") ?? ""
        algorithm = defaults.string(forKey: "algorithm") ?? ""
    }

    func persist() {
        defaults.set(walletName, forKey: "walletName")
        defaults.set(writeEndpointText, forKey: "writeEndpoint")
        defaults.set(rpcToken, forKey: "rpcToken")
        defaults.set(address, forKey: "address")
        defaults.set(algorithm, forKey: "algorithm")
    }

    func createWallet() {
        let name = walletName
        clearBalanceForRPC()
        run { client in
            let record = try client.createwallet(name: name)
            let balance = try client.balance(name: name)
            return .wallet(record, balance, "Created \(record.name) on the node. Keys stay in the node wallet store.")
        }
    }

    func loadWallet() {
        let name = walletName
        clearBalanceForRPC()
        run { client in
            let record = try client.walletInfo(name: name)
            let balance = try client.balance(name: name)
            return .wallet(record, balance, "Loaded \(record.name) from write RPC.")
        }
    }

    func refresh() {
        let name = walletName
        clearBalanceForRPC()
        nodeLine = ""
        run { client in
            let record = try client.walletInfo(name: name)
            let balance = try client.balance(name: name)
            let info = try client.getinfo()
            return .refresh(record, balance, info)
        }
    }

    func send(to: String, amountText: String, feeText: String) {
        let name = walletName
        clearBalanceForRPC()
        run { client in
            let amount = try Amount.parseWhole(amountText)
            let fee = try Amount.parseOptionalFee(feeText)
            let reply = try client.send(name: name, to: try Address.validate(to), amount: amount, fee: fee)
            let record = try client.walletInfo(name: name)
            let balance = try client.balance(name: name)
            return .sent(record, balance, reply)
        }
    }

    func loadPublicRead() {
        busy = true
        publicReadLine = ""
        Task.detached {
            do {
                let line = try HTTPReadRPC.get(
                    command: "getinfo",
                    url: "https://rpc.additionblockchain.com/rpc",
                    timeout: 8,
                    write: false
                )
                let info = try WriteRPCPolicy.parseGetinfo(line)
                let network = info["network"] ?? ""
                let height = info["height"] ?? ""
                await self.finishPublicRead("public read getinfo network=\(network) height=\(height)")
            } catch {
                await self.finishPublicRead(Self.display(error))
            }
        }
    }

    private func clearBalanceForRPC() {
        balanceReady = false
        balanceText = ""
    }

    private func run(_ work: @escaping (WalletRPCClient) throws -> SessionResult) {
        persist()
        busy = true
        status = "Calling write RPC…"
        let endpointText = writeEndpointText
        let token = rpcToken
        Task.detached {
            do {
                let endpoint = try WriteEndpoint.parse(endpointText, token: token)
                let client = WalletRPCClient(rpc: try TextRPCClient(endpoint: endpoint))
                let result = try work(client)
                await self.apply(result)
            } catch {
                await self.fail(Self.display(error))
            }
        }
    }

    private func apply(_ result: SessionResult) {
        switch result {
        case .wallet(let record, let balance, let message):
            address = record.address
            algorithm = record.algorithm
            balanceText = "\(balance) ADD"
            balanceReady = true
            status = message
        case .refresh(let record, let balance, let info):
            address = record.address
            algorithm = record.algorithm
            balanceText = "\(balance) ADD"
            balanceReady = true
            nodeLine = "network=\(info["network"] ?? "") height=\(info["height"] ?? "")"
            status = "Write RPC reachable."
        case .sent(let record, let balance, let reply):
            address = record.address
            algorithm = record.algorithm
            balanceText = "\(balance) ADD"
            balanceReady = true
            lastSend = reply
            status = reply
        }
        persist()
        busy = false
    }

    private func unusedSessionResult(_ value: Never) -> Never {
        switch value {}
    }

    private func fail(_ message: String) {
        busy = false
        balanceReady = false
        balanceText = ""
        status = message
    }

    private func finishPublicRead(_ line: String) {
        publicReadLine = line
        busy = false
    }

    private static func display(_ error: Error) -> String {
        if let addition = error as? AdditionError {
            return addition.errorDescription ?? "RPC offline"
        }
        return "RPC offline"
    }
}

private enum SessionResult {
    case wallet(WalletRecord, UInt64, String)
    case refresh(WalletRecord, UInt64, [String: String])
    case sent(WalletRecord, UInt64, String)
}
