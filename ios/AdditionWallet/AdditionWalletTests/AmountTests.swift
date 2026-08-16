import XCTest
@testable import AdditionWallet

final class AmountTests: XCTestCase {
    func testWholeUnitsOnly() throws {
        XCTAssertEqual(try Amount.parseWhole("10"), 10)
        XCTAssertEqual(try Amount.parseWhole("1"), 1)
        for raw in ["10.5", "0.00000001", "1e2", "1/2", "0", "-3", "", "ten"] {
            XCTAssertThrowsError(try Amount.parseWhole(raw))
        }
    }
}
