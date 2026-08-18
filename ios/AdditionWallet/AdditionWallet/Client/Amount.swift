import Foundation

enum Amount {
    static func parseWhole(_ raw: String) throws -> UInt64 {
        let text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty {
            throw AdditionError.invalidAmount
        }
        if text.contains(where: { ".,eE/".contains($0) }) {
            throw AdditionError.invalidAmount
        }
        if text.hasPrefix("-") {
            throw AdditionError.invalidAmount
        }
        guard text.allSatisfy(\.isNumber), let value = UInt64(text), value > 0 else {
            throw AdditionError.invalidAmount
        }
        return value
    }

    static func parseOptionalFee(_ raw: String?) throws -> UInt64? {
        guard let raw else { return nil }
        let text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty {
            return nil
        }
        if text == "0" {
            return 0
        }
        return try parseWhole(text)
    }
}
