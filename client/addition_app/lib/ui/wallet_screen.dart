import 'package:flutter/material.dart';

import 'core_shell.dart';

/// Backward-compatible entry used by older tests; Addition Core shell.
class WalletScreen extends StatelessWidget {
  const WalletScreen({super.key, this.autoConnect = true});

  final bool autoConnect;

  @override
  Widget build(BuildContext context) {
    return CoreShell(autoConnect: autoConnect, enableVideo: false);
  }
}
