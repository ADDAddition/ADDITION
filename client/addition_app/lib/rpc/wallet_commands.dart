import 'write_rpc_policy.dart';

const hashCommittedAddressHexLen = 128;

String validateWalletName(String name) {
  if (name.isEmpty || name.length > 64) {
    throw AdditionRpcException(
      'invalid wallet name (use 1-64 letters, digits, _ or -)',
    );
  }
  final first = name.codeUnitAt(0);
  final firstOk =
      (first >= 48 && first <= 57) ||
      (first >= 65 && first <= 90) ||
      (first >= 97 && first <= 122);
  if (!firstOk) {
    throw AdditionRpcException(
      'invalid wallet name (use 1-64 letters, digits, _ or -)',
    );
  }
  for (var i = 0; i < name.length; i++) {
    final c = name.codeUnitAt(i);
    final ok =
        (c >= 48 && c <= 57) ||
        (c >= 65 && c <= 90) ||
        (c >= 97 && c <= 122) ||
        c == 95 ||
        c == 45;
    if (!ok) {
      throw AdditionRpcException(
        'invalid wallet name (use 1-64 letters, digits, _ or -)',
      );
    }
  }
  return name;
}

String validateAddress(String address) {
  final raw = address.trim();
  if (raw.toLowerCase().startsWith('0x')) {
    throw AdditionRpcException('not an ADDITION address');
  }
  if (raw.length != hashCommittedAddressHexLen || !_looksLikeHex(raw)) {
    throw AdditionRpcException('ADDITION address must be 128 hex characters');
  }
  return raw.toLowerCase();
}

int parseWholeAmount(String raw) {
  final text = raw.trim();
  if (text.isEmpty) {
    throw AdditionRpcException('amount must be a whole ADD unit');
  }
  if (text.contains('.') ||
      text.contains(',') ||
      text.contains('e') ||
      text.contains('E') ||
      text.contains('/')) {
    throw AdditionRpcException('whole-unit amounts only; no decimal subunit');
  }
  if (text.startsWith('-')) {
    throw AdditionRpcException('amount must be > 0');
  }
  if (!_isDigits(text)) {
    throw AdditionRpcException('amount must be a whole ADD unit');
  }
  final value = int.parse(text);
  if (value <= 0) {
    throw AdditionRpcException('amount must be > 0');
  }
  return value;
}

int? parseOptionalFee(String? raw) {
  if (raw == null) return null;
  final text = raw.trim();
  if (text.isEmpty) return null;
  if (text == '0') return 0;
  return parseWholeAmount(text);
}

String buildCreatewallet(String name) =>
    'createwallet ${validateWalletName(name)}';

String buildWalletInfo(String name) =>
    'wallet_info ${validateWalletName(name)}';

String buildWalletList() => 'wallet_list';

String buildWalletBalance(String name) =>
    'wallet_balance ${validateWalletName(name)}';

String buildGetbalance(String address) =>
    'getbalance ${validateAddress(address)}';

String buildWalletSend(
  String name,
  String toAddr,
  int amount, {
  int? fee,
}) {
  validateWalletName(name);
  validateAddress(toAddr);
  if (amount <= 0) {
    throw AdditionRpcException('amount must be > 0');
  }
  if (fee != null && fee < 0) {
    throw AdditionRpcException('fee must be >= 0');
  }
  var line = 'wallet_send $name $toAddr $amount';
  if (fee != null) line += ' $fee';
  return line;
}

String buildGetinfo() => 'getinfo';

String buildFeeInfo() => 'fee_info';

String buildMine(String address, {int? threads}) {
  final addr = validateAddress(address);
  if (threads == null || threads <= 0) {
    return 'mine $addr';
  }
  return 'mine $addr $threads';
}

String buildPeers() => 'peers';

String buildMonetaryInfo() => 'monetary_info';

bool _looksLikeHex(String value) {
  if (value.isEmpty || value.length.isOdd) return false;
  for (var i = 0; i < value.length; i++) {
    final c = value.codeUnitAt(i);
    final ok =
        (c >= 48 && c <= 57) ||
        (c >= 65 && c <= 70) ||
        (c >= 97 && c <= 102);
    if (!ok) return false;
  }
  return true;
}

bool _isDigits(String value) {
  if (value.isEmpty) return false;
  for (var i = 0; i < value.length; i++) {
    final c = value.codeUnitAt(i);
    if (c < 48 || c > 57) return false;
  }
  return true;
}
