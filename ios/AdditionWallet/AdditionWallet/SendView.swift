import SwiftUI

struct SendView: View {
    @EnvironmentObject private var session: WalletSession
    @State private var to = ""
    @State private var amount = ""
    @State private var fee = ""

    var body: some View {
        ScreenBackground {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    BrandWordmark(height: 24)
                    Text("Send ADD")
                        .font(.title2.weight(.semibold))
                        .foregroundStyle(AdditionTheme.cream)
                    Card {
                        AssetRow(amountLabel: session.assetAmountLabel)
                    }
                    Card {
                        Text("Amount")
                            .font(.caption.weight(.medium))
                            .foregroundStyle(AdditionTheme.mute)
                        TextField("Whole units", text: $amount)
                            .keyboardType(.numberPad)
                            .font(.title3.monospaced())
                            .foregroundStyle(AdditionTheme.cream)
                        Text("Fee (blank uses node fee_info)")
                            .font(.caption.weight(.medium))
                            .foregroundStyle(AdditionTheme.mute)
                        TextField("1", text: $fee)
                            .keyboardType(.numberPad)
                            .font(.body.monospaced())
                            .foregroundStyle(AdditionTheme.cream)
                    }
                    Card {
                        Text("To")
                            .font(.caption.weight(.medium))
                            .foregroundStyle(AdditionTheme.mute)
                        TextField("128-hex ADDITION address", text: $to, axis: .vertical)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .font(.footnote.monospaced())
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
