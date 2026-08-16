import SwiftUI

struct ScreenBackground<Content: View>: View {
    @ViewBuilder var content: () -> Content

    var body: some View {
        ZStack {
            AdditionTheme.ink.ignoresSafeArea()
            content()
        }
    }
}

struct BrandWordmark: View {
    var height: CGFloat = 28

    var body: some View {
        Image(BrandImage.logo)
            .resizable()
            .scaledToFit()
            .frame(height: height)
            .accessibilityLabel("ADDITION")
    }
}

struct BrandMark: View {
    var size: CGFloat = 40

    var body: some View {
        Image(BrandImage.mark)
            .resizable()
            .scaledToFill()
            .frame(width: size, height: size)
            .clipShape(RoundedRectangle(cornerRadius: size * 0.22, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: size * 0.22, style: .continuous)
                    .stroke(AdditionTheme.line, lineWidth: 1)
            )
            .accessibilityLabel("ADD")
    }
}

struct Card<Content: View>: View {
    @ViewBuilder var content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            content()
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(AdditionTheme.panel)
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .stroke(AdditionTheme.line, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
    }
}

struct CircleAction: View {
    let title: String
    let systemImage: String
    let enabled: Bool
    let work: () -> Void

    var body: some View {
        Button(action: work) {
            VStack(spacing: 8) {
                ZStack {
                    Circle()
                        .fill(AdditionTheme.panelLift)
                        .frame(width: 56, height: 56)
                    Image(systemName: systemImage)
                        .font(.system(size: 20, weight: .semibold))
                        .foregroundStyle(enabled ? AdditionTheme.cream : AdditionTheme.mute)
                }
                Text(title)
                    .font(.footnote.weight(.medium))
                    .foregroundStyle(AdditionTheme.mute)
            }
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

struct AssetRow: View {
    let amountLabel: String

    var body: some View {
        HStack(spacing: 12) {
            BrandMark(size: 44)
            VStack(alignment: .leading, spacing: 2) {
                Text("ADD")
                    .font(.body.weight(.semibold))
                    .foregroundStyle(AdditionTheme.cream)
                Text("ADDITION")
                    .font(.caption)
                    .foregroundStyle(AdditionTheme.mute)
            }
            Spacer()
            Text(amountLabel)
                .font(.body.monospaced().weight(.medium))
                .foregroundStyle(AdditionTheme.cream)
        }
        .padding(.vertical, 4)
    }
}

struct PrimaryButton: View {
    let title: String
    let enabled: Bool
    let work: () -> Void

    var body: some View {
        Button(action: work) {
            Text(title)
                .font(.body.weight(.semibold))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 14)
                .foregroundStyle(AdditionTheme.cream)
                .background(enabled ? AdditionTheme.red : AdditionTheme.panelLift)
                .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

func shortAddress(_ address: String) -> String {
    if address.count <= 16 {
        return address
    }
    return "\(address.prefix(8))…\(address.suffix(8))"
}
