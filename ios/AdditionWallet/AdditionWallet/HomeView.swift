import SwiftUI

struct HomeView: View {
    @EnvironmentObject private var session: WalletSession

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("ADDITION")
                        .font(.system(size: 34, weight: .bold, design: .default))
                        .foregroundStyle(AdditionTheme.red)
                    Text("Research testnet wallet. Local or LAN node only. Not a hosted web wallet.")
                        .foregroundStyle(AdditionTheme.mute)
                    panel {
                        labeled("Wallet name", session.walletName)
                        labeled("Address", session.address.isEmpty ? "none loaded" : session.address)
                        labeled("Algorithm", session.algorithm.isEmpty ? "—" : session.algorithm)
                        if session.balanceReady {
                            labeled("Balance", session.balanceText)
                        } else {
                            labeled("Balance", "unavailable")
                        }
                    }
                    panel {
                        Text(session.status)
                            .foregroundStyle(AdditionTheme.cream)
                            .textSelection(.enabled)
                    }
                    HStack {
                        action("Create") { session.createWallet() }
                        action("Load") { session.loadWallet() }
                        action("Refresh") { session.refresh() }
                    }
                    Text("createwallet / wallet_info / wallet_balance talk to write RPC on a node you control. If that RPC is down, this screen shows RPC offline and does not invent an ADD amount.")
                        .font(.footnote)
                        .foregroundStyle(AdditionTheme.mute)
                    Text(WriteRPCPolicy.contact)
                        .font(.footnote)
                        .foregroundStyle(AdditionTheme.mute)
                }
                .padding(20)
            }
            .background(AdditionTheme.ink.ignoresSafeArea())
            .navigationBarHidden(true)
            .disabled(session.busy)
        }
    }

    private func labeled(_ title: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title.uppercased())
                .font(.caption)
                .foregroundStyle(AdditionTheme.mute)
            Text(value)
                .font(.body.monospaced())
                .foregroundStyle(AdditionTheme.cream)
                .textSelection(.enabled)
        }
    }

    private func panel<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            content()
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(AdditionTheme.panel)
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(AdditionTheme.line, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func action(_ title: String, _ work: @escaping () -> Void) -> some View {
        Button(title, action: work)
            .buttonStyle(.borderedProminent)
            .tint(AdditionTheme.red)
    }
}
