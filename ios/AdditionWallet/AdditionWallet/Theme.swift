import SwiftUI

enum AdditionTheme {
    // Mode pixel of web/public/logo-transparent.png (same red on apple-touch-icon / og).
    static let logoRedHex = "E61D16"
    static let ink = Color(red: 0.00, green: 0.00, blue: 0.00)
    static let panel = Color(red: 0.09, green: 0.09, blue: 0.10)
    static let panelLift = Color(red: 0.13, green: 0.13, blue: 0.14)
    static let line = Color(red: 0.22, green: 0.22, blue: 0.23)
    static let red = Color(
        red: 230.0 / 255.0,
        green: 29.0 / 255.0,
        blue: 22.0 / 255.0
    )
    static let cream = Color.white
    static let mute = Color(red: 0.63, green: 0.63, blue: 0.65)
}

enum BrandImage {
    static let logo = "Logo"
    static let mark = "Mark"
    static let openGraph = "OpenGraph"
    static let favicon = "Favicon"
}
