import XCTest
@testable import AdditionWallet

final class SHA3Tests: XCTestCase {
    func testEmptyAndABC() {
        XCTAssertEqual(
            SHA3_512.hexDigest(Data()),
            "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6" +
            "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26"
        )
        XCTAssertEqual(
            SHA3_512.hexDigest(Data("abc".utf8)),
            "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e" +
            "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0"
        )
    }
}
