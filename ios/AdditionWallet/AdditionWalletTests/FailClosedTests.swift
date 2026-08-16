import XCTest
@testable import AdditionWallet

private final class ScriptedTransport: RPCTransport, @unchecked Sendable {
    var replies: [String]
    var online: Bool
    var sent: [String] = []

    init(replies: [String], online: Bool = true) {
        self.replies = replies
        self.online = online
    }

    func send(_ wire: String) throws -> String {
        sent.append(wire)
        if !online {
            throw AdditionError.rpcOffline
        }
        if replies.isEmpty {
            return "error: unknown command"
        }
        return replies.removeFirst()
    }
}

final class FailClosedTests: XCTestCase {
    func testOfflineDoesNotInventBalance() throws {
        let transport = ScriptedTransport(replies: [], online: false)
        let rpc = try TextRPCClient(endpoint: try WriteEndpoint.parse("127.0.0.1:8545"), transport: transport)
        let client = WalletRPCClient(rpc: rpc)
        XCTAssertThrowsError(try client.balance(name: "demo")) { error in
            XCTAssertEqual(error as? AdditionError, .rpcOffline)
        }
        XCTAssertThrowsError(try WriteRPCPolicy.parseConfirmedBalance(""))
        XCTAssertThrowsError(try WriteRPCPolicy.parseConfirmedBalance("RPC offline"))
        XCTAssertThrowsError(try WriteRPCPolicy.parseGetinfo("ok but no fields"))
        for line in ["confirmed=abc", "balance=50", "ok", "50.0", "null"] {
            XCTAssertThrowsError(try WriteRPCPolicy.parseConfirmedBalance(line), line)
        }
    }

    func testCreateLoadSendCommands() throws {
        let pub = String(repeating: "bb", count: 32)
        let addr = try Address.derive(pubkeyHex: pub)
        let to = try Address.derive(pubkeyHex: String(repeating: "cc", count: 32))
        let transport = ScriptedTransport(replies: [
            "address=\(addr) address_chars=128 pub=\(pub) algo=ml-dsa-87 name=demo path=x priv_printed=0",
            "name=demo address=\(addr) confirmed=50 staked=0",
            "name=demo address=\(addr) pub=\(pub) algo=ml-dsa-87 confirmed=50",
            "ok:gossiped hash=abcd from=\(addr) to=\(to) amount=10 fee=1",
        ])
        let rpc = try TextRPCClient(endpoint: try WriteEndpoint.parse("127.0.0.1:8545"), transport: transport)
        let client = WalletRPCClient(rpc: rpc)
        let created = try client.createwallet(name: "demo")
        XCTAssertEqual(created.address, addr)
        XCTAssertEqual(try client.balance(name: "demo"), 50)
        let sent = try client.send(name: "demo", to: to, amount: 10, fee: 1)
        XCTAssertTrue(sent.contains("ok:gossiped"))
        XCTAssertEqual(transport.sent[0], "createwallet demo")
        XCTAssertEqual(transport.sent[1], "wallet_balance demo")
        XCTAssertTrue(transport.sent[2].hasPrefix("wallet_info "))
        XCTAssertEqual(transport.sent[3], "wallet_send demo \(to) 10 1")
    }

    func testRefusesInsecureAndForeignCommands() {
        for cmd in ["sendtx leaked", "sendtx_hash leaked", "sign_message leaked"] {
            XCTAssertThrowsError(try WriteRPCPolicy.assertCommand(cmd, write: true))
        }
        for cmd in ["eth_blockNumber", "getbalancebtc", "solana_send"] {
            XCTAssertThrowsError(try WriteRPCPolicy.assertCommand(cmd, write: true))
        }
    }

    func testActivityFromRealSendOnly() {
        let item = ActivityItem.fromSendReply("ok:gossiped hash=abcd from=aa to=bb amount=10 fee=1")
        XCTAssertEqual(item.kind, .send)
        XCTAssertEqual(item.amountLabel, "-10 ADD")
        XCTAssertEqual(item.detail, "to bb")
    }

    func testCommandBuilders() throws {
        XCTAssertEqual(try WalletCommands.createwallet(name: "default"), "createwallet default")
        let addr = String(repeating: "ab", count: 64)
        XCTAssertEqual(try WalletCommands.getbalance(address: addr), "getbalance \(addr)")
    }
}
