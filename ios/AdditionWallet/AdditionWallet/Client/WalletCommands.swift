import Foundation

enum WalletCommands {
    static func createwallet(name: String = "default") throws -> String {
        "createwallet \(try Address.validateWalletName(name))"
    }

    static func walletInfo(name: String) throws -> String {
        "wallet_info \(try Address.validateWalletName(name))"
    }

    static func walletList() -> String {
        "wallet_list"
    }

    static func walletBalance(name: String) throws -> String {
        "wallet_balance \(try Address.validateWalletName(name))"
    }

    static func getbalance(address: String) throws -> String {
        "getbalance \(try Address.validate(address))"
    }

    static func walletSend(name: String, to: String, amount: UInt64, fee: UInt64?) throws -> String {
        _ = try Address.validateWalletName(name)
        _ = try Address.validate(to)
        if amount == 0 {
            throw AdditionError.invalidAmount
        }
        var line = "wallet_send \(name) \(to) \(amount)"
        if let fee {
            line += " \(fee)"
        }
        return line
    }

    static func getinfo() -> String {
        "getinfo"
    }

    static func feeInfo() -> String {
        "fee_info"
    }
}
