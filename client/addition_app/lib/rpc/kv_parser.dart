/// Parse one-line TEXT RPC `key=value` replies.
Map<String, String> parseKv(String line) {
  final out = <String, String>{};
  for (final token in line.split(RegExp(r'\s+'))) {
    final idx = token.indexOf('=');
    if (idx <= 0) continue;
    out[token.substring(0, idx)] = token.substring(idx + 1);
  }
  return out;
}

String firstCommandToken(String command) {
  final parts = command.trim().split(RegExp(r'\s+'));
  return parts.isEmpty ? '' : parts.first;
}
