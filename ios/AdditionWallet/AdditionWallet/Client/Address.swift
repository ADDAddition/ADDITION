import Foundation

enum Address {
    static let hexLength = 128
    static let defaultScheme = "ml-dsa-87"

    static func looksLikeHex(_ value: String) -> Bool {
        if value.isEmpty || value.count % 2 != 0 {
            return false
        }
        return value.allSatisfy { character in
            character.isHexDigit
        }
    }

    static func validate(_ address: String) throws -> String {
        let raw = address.trimmingCharacters(in: .whitespacesAndNewlines)
        if raw.lowercased().hasPrefix("0x") {
            throw AdditionError.invalidAddress
        }
        if raw.count != hexLength || !looksLikeHex(raw) {
            throw AdditionError.invalidAddress
        }
        return raw.lowercased()
    }

    static func derive(pubkeyHex: String, schemeId: String = defaultScheme) throws -> String {
        if schemeId.isEmpty {
            throw AdditionError.missingField("scheme_id")
        }
        if !looksLikeHex(pubkeyHex) {
            throw AdditionError.rpc("invalid pubkey hex")
        }
        var preimage = Data(schemeId.utf8)
        preimage.append(0)
        guard let pubkey = Data(hexString: pubkeyHex) else {
            throw AdditionError.rpc("invalid pubkey hex")
        }
        preimage.append(pubkey)
        return SHA3_512.hexDigest(preimage)
    }

    static func validateWalletName(_ name: String) throws -> String {
        if name.isEmpty || name.count > 64 {
            throw AdditionError.invalidWalletName
        }
        guard let first = name.first, first.isLetter || first.isNumber else {
            throw AdditionError.invalidWalletName
        }
        let allowed = name.allSatisfy { character in
            character.isLetter || character.isNumber || character == "_" || character == "-"
        }
        if !allowed {
            throw AdditionError.invalidWalletName
        }
        return name
    }
}

private extension Data {
    init?(hexString: String) {
        let raw = hexString.lowercased()
        if raw.count % 2 != 0 {
            return nil
        }
        var data = Data(capacity: raw.count / 2)
        var index = raw.startIndex
        while index < raw.endIndex {
            let next = raw.index(index, offsetBy: 2)
            guard let byte = UInt8(raw[index..<next], radix: 16) else {
                return nil
            }
            data.append(byte)
            index = next
        }
        self = data
    }
}
