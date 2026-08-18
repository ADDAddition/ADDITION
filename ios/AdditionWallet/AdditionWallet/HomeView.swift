import SwiftUI

struct HomeView: View {
    @EnvironmentObject private var session: WalletSession

    var body: some View {
        NavigationStack {
            ScreenBackground {
                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        header
                        balance
                        actions
                        assetCard
                        if !session.hasWallet {
                            setupCard
                        }
                        recent
                    }
                    .padding(20)
                }
            }
            .toolbar(.hidden, for: .navigationBar)
            .disabled(session.busy)
        }
    }

    private var header: some View {
        HStack {
            BrandWordmark(height: 26)
            Spacer()
            Button {
                session.showNode = true
            } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 18, weight: .medium))
                    .foregroundStyle(AdditionTheme.cream)
                    .frame(width: 36, height: 36)
                    .background(AdditionTheme.panel)
                    .clipShape(Circle())
            }
            .accessibilityLabel("Node")
        }
    }

    private var balance: some View {
        VStack(alignment: .leading, spacing: 8) {
            SectionLabel(title: "Balance")
            if session.balanceReady {
                Text(session.balanceText)
                    .font(.system(size: 40, weight: .semibold, design: .rounded))
                    .foregroundStyle(AdditionTheme.cream)
                    .minimumScaleFactor(0.55)
                    .lineLimit(1)
            } else {
                Text(session.status.contains("RPC offline") ? "RPC offline" : "unavailable")
                    .font(.system(size: 34, weight: .semibold, design: .rounded))
                    .foregroundStyle(AdditionTheme.cream)
            }
            Text(session.hasWallet ? shortAddress(session.address) : "No wallet loaded")
                .font(.footnote.monospaced())
                .foregroundStyle(AdditionTheme.mute)
                .textSelection(.enabled)
            Text("Research testnet")
                .font(.caption)
                .foregroundStyle(AdditionTheme.mute)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.top, 4)
    }

    private var actions: some View {
        HStack(spacing: 28) {
            CircleAction(title: "Receive", systemImage: "arrow.down", enabled: true) {
                session.selectedTab = .receive
            }
            CircleAction(title: "Send", systemImage: "arrow.up", enabled: session.hasWallet) {
                session.selectedTab = .send
            }
            CircleAction(title: "Refresh", systemImage: "arrow.clockwise", enabled: true) {
                if session.hasWallet {
                    session.refresh()
                } else {
                    session.loadWallet()
                }
            }
        }
        .frame(maxWidth: .infinity)
    }

    private var assetCard: some View {
        Card {
            SectionLabel(title: "Asset")
            AssetRow(amountLabel: session.assetAmountLabel)
        }
    }

    private var setupCard: some View {
        Card {
            SectionLabel(title: "Wallet")
            Text("Create or load on a write RPC node you control.")
                .font(.footnote)
                .foregroundStyle(AdditionTheme.mute)
            HStack(spacing: 10) {
                PrimaryButton(title: "Create", enabled: !session.busy) {
                    session.createWallet()
                }
                SecondaryButton(title: "Load", enabled: !session.busy) {
                    session.loadWallet()
                }
            }
            Text(session.status)
                .font(.caption)
                .foregroundStyle(AdditionTheme.mute)
                .textSelection(.enabled)
        }
    }

    private var recent: some View {
        Card {
            HStack {
                SectionLabel(title: "Activity")
                Spacer()
                Button("See all") {
                    session.selectedTab = .activity
                }
                .font(.caption.weight(.medium))
                .foregroundStyle(AdditionTheme.red)
            }
            if let item = session.activity.first {
                ActivityRow(item: item)
            } else {
                Text("No activity from write RPC yet.")
                    .font(.footnote)
                    .foregroundStyle(AdditionTheme.mute)
            }
            if session.hasWallet {
                Text(session.status)
                    .font(.caption)
                    .foregroundStyle(AdditionTheme.mute)
                    .textSelection(.enabled)
            }
        }
    }
}

#Preview("Home") {
    HomeView()
        .environmentObject(WalletSession())
}
