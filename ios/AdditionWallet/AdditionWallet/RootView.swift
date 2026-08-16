import SwiftUI

struct RootView: View {
    @EnvironmentObject private var session: WalletSession

    var body: some View {
        TabView(selection: $session.selectedTab) {
            HomeView()
                .tabItem { Label("Home", systemImage: "house.fill") }
                .tag(WalletTab.home)
            ReceiveView()
                .tabItem { Label("Receive", systemImage: "arrow.down.left") }
                .tag(WalletTab.receive)
            SendView()
                .tabItem { Label("Send", systemImage: "arrow.up.right") }
                .tag(WalletTab.send)
            ActivityView()
                .tabItem { Label("Activity", systemImage: "clock") }
                .tag(WalletTab.activity)
        }
        .tint(AdditionTheme.red)
        .preferredColorScheme(.dark)
        .sheet(isPresented: $session.showNode) {
            NodeView()
                .environmentObject(session)
        }
    }
}

#Preview("Root") {
    RootView()
        .environmentObject(WalletSession())
}
