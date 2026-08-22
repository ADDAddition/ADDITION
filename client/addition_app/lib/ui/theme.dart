import 'package:flutter/material.dart';

/// Brand colors sampled from the ADDITION wordmark (#E61D16).
class AdditionTheme {
  static const logoRed = Color(0xFFE61D16);
  static const ink = Color(0xFF000000);
  static const panel = Color(0xFF17171A);
  static const panelLift = Color(0xFF212124);
  static const line = Color(0xFF38383B);
  static const cream = Color(0xFFFFFFFF);
  static const mute = Color(0xFFA0A0A6);

  static ThemeData dark() {
    final base = ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      colorScheme: ColorScheme.fromSeed(
        seedColor: logoRed,
        brightness: Brightness.dark,
      ),
      scaffoldBackgroundColor: ink,
    );
    return base.copyWith(
      appBarTheme: const AppBarTheme(
        backgroundColor: ink,
        foregroundColor: cream,
        elevation: 0,
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: panel,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: const BorderSide(color: line),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: const BorderSide(color: line),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: const BorderSide(color: logoRed),
        ),
        labelStyle: const TextStyle(color: mute),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: logoRed,
          foregroundColor: cream,
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: cream,
          side: const BorderSide(color: line),
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        ),
      ),
    );
  }
}
