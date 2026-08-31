import 'package:flutter/material.dart';

import 'ui/core_shell.dart';
import 'ui/theme.dart';

void main() {
  runApp(const AdditionApp());
}

class AdditionApp extends StatelessWidget {
  const AdditionApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Addition Core',
      debugShowCheckedModeBanner: false,
      theme: AdditionTheme.dark(),
      home: const CoreShell(),
    );
  }
}
