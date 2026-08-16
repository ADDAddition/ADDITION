import SwiftUI

@main
struct AdditionWalletApp: App {
    @StateObject private var session = WalletSession()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(session)
        }
    }
}
