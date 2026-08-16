import SwiftUI

struct NodeView: View {
    @EnvironmentObject private var session: WalletSession

    var body: some View {
        NavigationStack {
            ScreenBackground {
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        Image(BrandImage.openGraph)
                            .resizable()
                            .scaledToFit()
                            .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
                            .accessibilityLabel("ADDITION")
                        Card {
                            Text("Write RPC")
                                .font(.caption.weight(.medium))
                                .foregroundStyle(AdditionTheme.mute)
                            TextField("127.0.0.1:8545 or LAN host", text: $session.writeEndpointText)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                                .font(.body.monospaced())
                                .foregroundStyle(AdditionTheme.cream)
                            TextField("Optional RPC token", text: $session.rpcToken)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                                .font(.body.monospaced())
                                .foregroundStyle(AdditionTheme.cream)
                            TextField("Wallet name", text: $session.walletName)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                                .font(.body.monospaced())
                                .foregroundStyle(AdditionTheme.cream)
                            Text("Loopback or your LAN node only. Public operator hosts are refused for createwallet, balances, and send.")
                                .font(.footnote)
                                .foregroundStyle(AdditionTheme.mute)
                        }
                        PrimaryButton(title: "Refresh write RPC", enabled: !session.busy) {
                            session.refresh()
                        }
                        Card {
                            Text(session.nodeLine.isEmpty ? session.status : session.nodeLine)
                                .font(.footnote.monospaced())
                                .foregroundStyle(AdditionTheme.cream)
                                .textSelection(.enabled)
                        }
                        Card {
                            Text("Public read only")
                                .font(.caption.weight(.medium))
                                .foregroundStyle(AdditionTheme.mute)
                            Text("getinfo / getblock / getblockraw. Not a wallet backend.")
                                .font(.footnote)
                                .foregroundStyle(AdditionTheme.mute)
                            PrimaryButton(title: "Public getinfo", enabled: !session.busy) {
                                session.loadPublicRead()
                            }
                            Text(session.publicReadLine.isEmpty ? "—" : session.publicReadLine)
                                .font(.footnote.monospaced())
                                .foregroundStyle(AdditionTheme.mute)
                                .textSelection(.enabled)
                        }
                        Text(WriteRPCPolicy.contact)
                            .font(.caption)
                            .foregroundStyle(AdditionTheme.mute)
                    }
                    .padding(20)
                }
            }
            .navigationTitle("Node")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        session.persist()
                        session.showNode = false
                    }
                    .foregroundStyle(AdditionTheme.red)
                }
            }
            .onDisappear { session.persist() }
        }
        .preferredColorScheme(.dark)
    }
}
