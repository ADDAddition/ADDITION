import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:addition_app/ui/theme.dart';
import 'package:addition_app/ui/wallet_screen.dart';

void main() {
  testWidgets('wallet app boots with honest chrome', (WidgetTester tester) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: AdditionTheme.dark(),
        home: const WalletScreen(autoConnect: false),
      ),
    );
    expect(find.text('ADDITION Wallet'), findsOneWidget);
    expect(find.textContaining('token sale'), findsOneWidget);
    expect(find.textContaining('loopback'), findsWidgets);
    expect(find.textContaining('Best Route'), findsNothing);
    expect(find.textContaining('A new Flutter project'), findsNothing);
  });
}
