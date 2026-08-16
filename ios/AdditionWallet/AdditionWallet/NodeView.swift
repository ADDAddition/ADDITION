import SwiftUI

struct NodeView: View {
    @EnvironmentObject private var session: WalletSession

    var body: some View {
        NavigationStack {
            Form {
                Section("Write RPC") {
                    TextField("127.0.0.1:8545 or LAN host", text: $session.writeEndpointText)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .font(.body.monospaced())
                    TextField("Optional RPC token", text: $session.rpcToken)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    TextField("Wallet name", text: $session.walletName)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Text("Default is 127.0.0.1:8545 (iOS Simulator on a Mac that already runs additiond). A phone needs your own loopback or LAN node. Public operator hosts are refused for createwallet, balances, and send.")
                        .font(.footnote)
                        .foregroundStyle(AdditionTheme.mute)
                }
                Section("Node you control") {
                    Button("Refresh write RPC getinfo") {
                        session.refresh()
                    }
                    Text(session.nodeLine.isEmpty ? session.status : session.nodeLine)
                        .font(.footnote.monospaced())
                        .textSelection(.enabled)
                }
                Section("Public read only") {
                    Button("Public getinfo") {
                        session.loadPublicRead()
                    }
                    Text("getinfo / getblock / getblockraw only. This is not a wallet backend.")
                        .font(.footnote)
                        .foregroundStyle(AdditionTheme.mute)
                    Text(session.publicReadLine.isEmpty ? "—" : session.publicReadLine)
                        .font(.footnote.monospaced())
                        .textSelection(.enabled)
                }
            }
            .scrollContentBackground(.hidden)
            .background(AdditionTheme.ink.ignoresSafeArea())
            .navigationTitle("Node")
            .onDisappear { session.persist() }
        }
    }
}
