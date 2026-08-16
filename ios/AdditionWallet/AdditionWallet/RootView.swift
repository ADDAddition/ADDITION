import SwiftUI

struct RootView: View {
    var body: some View {
        TabView {
            HomeView()
                .tabItem { Label("Wallet", systemImage: "square.stack.3d.up") }
            ReceiveView()
                .tabItem { Label("Receive", systemImage: "arrow.down.left") }
            SendView()
                .tabItem { Label("Send", systemImage: "arrow.up.right") }
            NodeView()
                .tabItem { Label("Node", systemImage: "antenna.radiowaves.left.and.right") }
        }
        .tint(AdditionTheme.red)
        .preferredColorScheme(.dark)
    }
}
