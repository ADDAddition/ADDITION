import XCTest
@testable import AdditionWallet

final class WriteRPCPolicyTests: XCTestCase {
    func testAllowsLoopbackAndLAN() throws {
        XCTAssertEqual(WriteRPCPolicy.classify(host: "127.0.0.1"), .loopback)
        XCTAssertEqual(WriteRPCPolicy.classify(host: "localhost"), .loopback)
        XCTAssertEqual(WriteRPCPolicy.classify(host: "::1"), .loopback)
        XCTAssertEqual(WriteRPCPolicy.classify(host: "192.168.1.20"), .lan)
        XCTAssertEqual(WriteRPCPolicy.classify(host: "10.0.0.8"), .lan)
        XCTAssertEqual(WriteRPCPolicy.classify(host: "172.16.4.4"), .lan)
        XCTAssertEqual(WriteRPCPolicy.classify(host: "macbook.local"), .lan)
        _ = try WriteRPCPolicy.assertWriteEndpoint("127.0.0.1:8545")
        _ = try WriteEndpoint.parse("http://192.168.1.20:18545")
    }

    func testRefusesPublicWriteEndpoints() {
        let refused = [
            "rpc.additionblockchain.com",
            "additionblockchain.com",
            "34.27.30.115",
            "8.8.8.8",
            "1.1.1.1",
            "https://rpc.additionblockchain.com/rpc",
            "http://34.27.30.115:38545/rpc",
        ]
        for host in refused {
            XCTAssertThrowsError(try WriteEndpoint.parse(host), host)
        }
    }

    func testPublicReadIsNotWalletBackend() throws {
        XCTAssertTrue(WriteRPCPolicy.isKnownPublicReadEndpoint("https://rpc.additionblockchain.com/rpc"))
        XCTAssertThrowsError(try WriteRPCPolicy.assertCommand("createwallet default", write: false))
        XCTAssertThrowsError(try WriteRPCPolicy.assertCommand("wallet_send demo aa 1", write: false))
        XCTAssertThrowsError(try WriteRPCPolicy.assertCommand("getbalance " + String(repeating: "ab", count: 64), write: false))
        _ = try WriteRPCPolicy.assertCommand("getinfo", write: false)
        _ = try WriteRPCPolicy.assertCommand("getblock 1", write: false)
        _ = try WriteRPCPolicy.assertCommand("getblockraw 1", write: false)
    }
}
