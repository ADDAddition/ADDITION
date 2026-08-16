import SwiftUI

struct SendView: View {
    @EnvironmentObject private var session: WalletSession
    @State private var to = ""
    @State private var amount = ""
    @State private var fee = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Destination") {
                    TextField("128-hex ADDITION address", text: $to, axis: .vertical)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .font(.body.monospaced())
                }
                Section("Whole units") {
                    TextField("Amount", text: $amount)
                        .keyboardType(.numberPad)
                    TextField("Fee (blank uses node fee_info)", text: $fee)
                        .keyboardType(.numberPad)
                }
                Section {
                    Button("Send") {
                        session.send(to: to, amountText: amount, feeText: fee)
                    }
                    .disabled(session.busy || session.address.isEmpty)
                }
                Section("Last write RPC reply") {
                    Text(session.lastSend.isEmpty ? session.status : session.lastSend)
                        .font(.footnote.monospaced())
                        .textSelection(.enabled)
                }
            }
            .scrollContentBackground(.hidden)
            .background(AdditionTheme.ink.ignoresSafeArea())
            .navigationTitle("Send ADD")
        }
    }
}
