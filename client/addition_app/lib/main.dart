import 'package:flutter/material.dart';

import 'ui/theme.dart';
import 'ui/wallet_screen.dart';

void main() {
  runApp(const AdditionApp());
}

class AdditionApp extends StatelessWidget {
  const AdditionApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ADDITION Wallet',
      debugShowCheckedModeBanner: false,
      theme: AdditionTheme.dark(),
      home: const WalletScreen(),
    );
  }
}
