import SwiftUI

struct SendView: View {
    @EnvironmentObject private var session: WalletSession
    @State private var to = ""
    @State private var amount = ""
    @State private var fee = ""

    var body: some View {
        ScreenBackground {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    BrandWordmark(height: 24)
                    ScreenTitle(
                        title: "Send",
                        subtitle: "Whole ADD units on a write RPC node you control."
                    )
                    Card {
                        SectionLabel(title: "Available")
                        AssetRow(amountLabel: session.assetAmountLabel)
                    }
                    Card {
                        SectionLabel(title: "Amount")
                        HStack(alignment: .firstTextBaseline, spacing: 8) {
                            TextField("0", text: $amount)
                                .keyboardType(.numberPad)
                                .font(.system(size: 32, weight: .semibold, design: .rounded).monospacedDigit())
                                .foregroundStyle(AdditionTheme.cream)
                            Text("ADD")
                                .font(.title3.weight(.semibold))
                                .foregroundStyle(AdditionTheme.mute)
                        }
                    }
                    Card {
                        SectionLabel(title: "To")
                        TextField("128-hex ADDITION address", text: $to, axis: .vertical)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .font(.footnote.monospaced())
                            .foregroundStyle(AdditionTheme.cream)
                    }
                    Card {
                        SectionLabel(title: "Fee")
                        TextField("Blank uses node fee_info", text: $fee)
                            .keyboardType(.numberPad)
                            .font(.body.monospaced())
                            .foregroundStyle(AdditionTheme.cream)
                    }
                    PrimaryButton(
                        title: session.busy ? "Sending…" : "Send",
                        enabled: !session.busy && session.hasWallet
                    ) {
                        session.send(to: to, amountText: amount, feeText: fee)
                    }
                    Text(session.lastSend.isEmpty ? session.status : session.lastSend)
                        .font(.caption.monospaced())
                        .foregroundStyle(AdditionTheme.mute)
                        .textSelection(.enabled)
                }
                .padding(20)
            }
        }
    }
}

#Preview("Send") {
    SendView()
        .environmentObject(WalletSession())
}
