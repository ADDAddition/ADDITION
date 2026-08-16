import XCTest
@testable import AdditionWallet

final class AddressTests: XCTestCase {
    func testDerivationMatchesNodeFormula() throws {
        let pub = String(repeating: "bb", count: 32)
        let addr = try Address.derive(pubkeyHex: pub)
        XCTAssertEqual(addr.count, 128)
        XCTAssertEqual(
            addr,
            "0d7423925f416bb9a12138d5dc4307735df023980d7d5c5b2858b00f57301f07" +
            "932d68659e94d26f89f1a3b707bc219a78668722ed44de7edfca2e18727e4a2a"
        )
        XCTAssertEqual(addr, try Address.derive(pubkeyHex: pub, schemeId: "ml-dsa-87"))
        XCTAssertNotEqual(addr, try Address.derive(pubkeyHex: pub, schemeId: "slh-dsa-shake-256s"))
        XCTAssertNotEqual(addr, pub)
        XCTAssertNotEqual(addr, try Address.derive(pubkeyHex: String(repeating: "00", count: 32)))
    }

    func testRejectsForeignAddressShapes() {
        XCTAssertThrowsError(try Address.validate("0x" + String(repeating: "ab", count: 20)))
        XCTAssertThrowsError(try Address.validate("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"))
        XCTAssertThrowsError(try Address.validate("1BoatSLRHtKNngkdXEeobR76b53LETtpyT"))
        XCTAssertThrowsError(try Address.validate("So11111111111111111111111111111111111111112"))
    }

    func testWalletNameMatchesNode() throws {
        XCTAssertEqual(try Address.validateWalletName("default"), "default")
        XCTAssertEqual(try Address.validateWalletName("Alice-1"), "Alice-1")
        for raw in ["", "-bad", "has space", String(repeating: "a", count: 65), "weird!"] {
            XCTAssertThrowsError(try Address.validateWalletName(raw))
        }
    }
}
