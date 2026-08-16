import SwiftUI

struct ActivityView: View {
    @EnvironmentObject private var session: WalletSession

    var body: some View {
        ScreenBackground {
            VStack(alignment: .leading, spacing: 16) {
                BrandWordmark(height: 24)
                ScreenTitle(
                    title: "Activity",
                    subtitle: "Only creates, loads, and sends from write RPC."
                )
                if session.activity.isEmpty {
                    Card {
                        HStack(spacing: 12) {
                            BrandMark(size: 40)
                            VStack(alignment: .leading, spacing: 4) {
                                Text("Nothing from write RPC yet")
                                    .font(.body.weight(.medium))
                                    .foregroundStyle(AdditionTheme.cream)
                                Text("Creates, loads, and sends you make on a node you control appear here. This list is not invented.")
                                    .font(.footnote)
                                    .foregroundStyle(AdditionTheme.mute)
                            }
                        }
                    }
                    Spacer()
                } else {
                    ScrollView {
                        VStack(spacing: 10) {
                            ForEach(session.activity) { item in
                                Card {
                                    ActivityRow(item: item)
                                }
                            }
                        }
                    }
                }
            }
            .padding(20)
        }
    }
}

struct ActivityRow: View {
    let item: ActivityItem

    var body: some View {
        HStack(spacing: 12) {
            ZStack {
                Circle()
                    .fill(AdditionTheme.panelLift)
                    .frame(width: 40, height: 40)
                Image(systemName: symbol)
                    .foregroundStyle(AdditionTheme.cream)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(item.title)
                    .font(.body.weight(.medium))
                    .foregroundStyle(AdditionTheme.cream)
                Text(item.detail)
                    .font(.caption.monospaced())
                    .foregroundStyle(AdditionTheme.mute)
                    .lineLimit(2)
            }
            Spacer()
            if let amount = item.amountLabel {
                Text(amount)
                    .font(.footnote.monospaced().weight(.medium))
                    .foregroundStyle(AdditionTheme.cream)
            }
        }
    }

    private var symbol: String {
        switch item.kind {
        case .send:
            return "arrow.up.right"
        case .created:
            return "plus"
        case .loaded:
            return "tray.and.arrow.down"
        case .node:
            return "antenna.radiowaves.left.and.right"
        }
    }
}

#Preview("Activity") {
    ActivityView()
        .environmentObject(WalletSession())
}
