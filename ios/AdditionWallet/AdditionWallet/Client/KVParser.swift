import Foundation

enum KVParser {
    static func parse(_ line: String) -> [String: String] {
        var out: [String: String] = [:]
        for token in line.split(whereSeparator: { $0.isWhitespace }) {
            let text = String(token)
            guard let split = text.firstIndex(of: "=") else { continue }
            let key = String(text[..<split])
            let value = String(text[text.index(after: split)...])
            out[key] = value
        }
        return out
    }

    static func firstToken(_ command: String) -> String {
        command.split(whereSeparator: { $0.isWhitespace }).first.map(String.init) ?? ""
    }
}
