import Foundation

/// FIPS 202 SHA3-512. Not Keccak-256. Used only for ADDITION hash-committed addresses.
enum SHA3_512 {
    static func digest(_ data: Data) -> Data {
        var state = [UInt64](repeating: 0, count: 25)
        let rate = 72
        let bytes = [UInt8](data)
        var offset = 0
        while offset + rate <= bytes.count {
            xorAbsorb(&state, bytes, offset: offset, length: rate)
            keccakF(&state)
            offset += rate
        }
        var block = [UInt8](repeating: 0, count: rate)
        let remain = bytes.count - offset
        if remain > 0 {
            for index in 0..<remain {
                block[index] = bytes[offset + index]
            }
        }
        block[remain] = 0x06
        block[rate - 1] |= 0x80
        xorAbsorb(&state, block, offset: 0, length: rate)
        keccakF(&state)
        var out = [UInt8](repeating: 0, count: 64)
        for index in 0..<8 {
            let word = state[index]
            for byte in 0..<8 {
                out[index * 8 + byte] = UInt8((word >> (8 * byte)) & 0xFF)
            }
        }
        return Data(out)
    }

    static func hexDigest(_ data: Data) -> String {
        digest(data).map { String(format: "%02x", $0) }.joined()
    }

    private static func xorAbsorb(_ state: inout [UInt64], _ bytes: [UInt8], offset: Int, length: Int) {
        let words = length / 8
        for index in 0..<words {
            var word: UInt64 = 0
            for byte in 0..<8 {
                word |= UInt64(bytes[offset + index * 8 + byte]) << (8 * byte)
            }
            state[index] ^= word
        }
    }

    private static func keccakF(_ state: inout [UInt64]) {
        let rotc: [Int] = [
            1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
            27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
        ]
        let piln: [Int] = [
            10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
            15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
        ]
        let rndc: [UInt64] = [
            0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
            0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
            0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
            0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
            0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
            0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008,
        ]

        for round in 0..<24 {
            var bc = [UInt64](repeating: 0, count: 5)
            for index in 0..<5 {
                bc[index] = state[index] ^ state[index + 5] ^ state[index + 10] ^ state[index + 15] ^ state[index + 20]
            }
            for index in 0..<5 {
                let temp = bc[(index + 4) % 5] ^ rotateLeft(bc[(index + 1) % 5], 1)
                for j in stride(from: 0, to: 25, by: 5) {
                    state[j + index] ^= temp
                }
            }

            var t = state[1]
            for index in 0..<24 {
                let j = piln[index]
                bc[0] = state[j]
                state[j] = rotateLeft(t, rotc[index])
                t = bc[0]
            }

            for j in stride(from: 0, to: 25, by: 5) {
                for index in 0..<5 {
                    bc[index] = state[j + index]
                }
                for index in 0..<5 {
                    state[j + index] ^= (~bc[(index + 1) % 5]) & bc[(index + 2) % 5]
                }
            }
            state[0] ^= rndc[round]
        }
    }

    private static func rotateLeft(_ value: UInt64, _ bits: Int) -> UInt64 {
        let shift = bits % 64
        if shift == 0 {
            return value
        }
        return (value << shift) | (value >> (64 - shift))
    }
}
