import SwiftUI
import UIKit

struct ReceiveView: View {
    @EnvironmentObject private var session: WalletSession
    @State private var copied = false

    var body: some View {
        ScreenBackground {
            VStack(spacing: 18) {
                BrandWordmark(height: 24)
                ScreenTitle(
                    title: "Receive",
                    subtitle: "Share this ADDITION address. Whole ADD units only."
                )
                QRCodeView(payload: session.address)
                if session.address.isEmpty {
                    Text("Create or load a wallet on a node you control.")
                        .font(.footnote)
                        .foregroundStyle(AdditionTheme.mute)
                        .multilineTextAlignment(.center)
                } else {
                    Card {
                        SectionLabel(title: "Address")
                        HStack(alignment: .top, spacing: 12) {
                            BrandMark(size: 36)
                            Text(session.address)
                                .font(.footnote.monospaced())
                                .foregroundStyle(AdditionTheme.cream)
                                .textSelection(.enabled)
                        }
                    }
                    PrimaryButton(title: copied ? "Copied" : "Copy address", enabled: true) {
                        UIPasteboard.general.string = session.address
                        copied = true
                    }
                }
                Text("128-hex hash-committed address.")
                    .font(.caption)
                    .foregroundStyle(AdditionTheme.mute)
                    .multilineTextAlignment(.center)
                Spacer()
            }
            .padding(20)
        }
    }
}

#Preview("Receive") {
    ReceiveView()
        .environmentObject(WalletSession())
}
