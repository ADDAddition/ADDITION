import Foundation

enum WalletTab: Hashable {
    case home
    case receive
    case send
    case activity
}

struct ActivityItem: Identifiable, Equatable, Codable {
    enum Kind: String, Codable {
        case send
        case created
        case loaded
        case node
    }

    var id: String
    var kind: Kind
    var title: String
    var detail: String
    var amountLabel: String?

    static func fromSendReply(_ reply: String) -> ActivityItem {
        let values = KVParser.parse(reply)
        let amount = values["amount"]
        let dest = values["to"].map(shortAddress) ?? ""
        let hash = values["hash"].map(shortAddress) ?? reply
        return ActivityItem(
            id: values["hash"] ?? UUID().uuidString,
            kind: .send,
            title: "Sent ADD",
            detail: dest.isEmpty ? hash : "to \(dest)",
            amountLabel: amount.map { "-\($0) ADD" }
        )
    }
}
