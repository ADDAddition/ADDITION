import SwiftUI
import UIKit

struct ReceiveView: View {
    @EnvironmentObject private var session: WalletSession
    @State private var copied = false

    var body: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 16) {
                Text("Receive ADD")
                    .font(.title.bold())
                    .foregroundStyle(AdditionTheme.cream)
                Text("Show this 128-hex hash-committed address. Whole ADD units only.")
                    .foregroundStyle(AdditionTheme.mute)
                Text(session.address.isEmpty ? "Load or create a wallet first." : session.address)
                    .font(.body.monospaced())
                    .foregroundStyle(AdditionTheme.cream)
                    .textSelection(.enabled)
                    .padding(16)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(AdditionTheme.panel)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                Button(copied ? "Copied" : "Copy address") {
                    guard !session.address.isEmpty else { return }
                    UIPasteboard.general.string = session.address
                    copied = true
                }
                .buttonStyle(.borderedProminent)
                .tint(AdditionTheme.red)
                .disabled(session.address.isEmpty)
                Spacer()
            }
            .padding(20)
            .background(AdditionTheme.ink.ignoresSafeArea())
        }
    }
}
