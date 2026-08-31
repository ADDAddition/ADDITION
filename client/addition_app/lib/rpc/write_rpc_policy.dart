import 'kv_parser.dart';

/// Write RPC policy for Addition Core (full-node desktop GUI).
///
/// Write calls stay on loopback only (127.0.0.1 / ::1 / localhost).
/// Never bind or target 0.0.0.0:8545 for send/keys.
/// Public read is allowlisted to known getinfo endpoints (no wallet_send).
class WriteRpcPolicy {
  static const defaultHost = '127.0.0.1';
  /// Mainnet local write default (coexists with testnet on :8545).
  static const defaultPort = 8546;
  static const maxRpcLine = 32768;
  static const contact = 'contact@additionblockchain.com';
  static const productNetworkId = 'ADDITION_MAINNET_V1';
  static const coinbaseReward = 50;

  static const publicReadCommands = {'getinfo', 'getblock', 'getblockraw'};

  static const writeCommands = {
    'createwallet',
    'wallet_list',
    'wallet_info',
    'wallet_balance',
    'wallet_send',
    'getbalance',
    'fee_info',
    'getinfo',
    'mine',
    'peers',
    'monetary_info',
    'protocol_status',
  };

  static const insecureCommands = {'sendtx', 'sendtx_hash', 'sign_message'};

  static const foreignChainTokens = {
    'bitcoin',
    'btc',
    'ethereum',
    'eth',
    'solana',
    'sol',
    'metamask',
    'walletconnect',
    'smartchain',
  };

  static const loopbackHosts = {
    '127.0.0.1',
    '::1',
    'localhost',
    'localhost.localdomain',
  };

  static const refusedPublicWriteHosts = {
    'rpc.additionblockchain.com',
    'additionblockchain.com',
    'www.additionblockchain.com',
    '34.27.30.115',
  };

  static const knownPublicReadUrls = {
    'https://rpc.additionblockchain.com/rpc',
    'https://additionblockchain.com/api/rpc',
    'http://34.27.30.115/rpc',
    'http://34.27.30.115:38545/rpc',
    'http://34.27.30.115:38546/rpc',
  };

  static const defaultPublicReadUrl =
      'https://additionblockchain.com/api/rpc';

  static String hostFromEndpoint(String endpoint) {
    final text = endpoint.trim();
    if (text.isEmpty) {
      throw AdditionRpcException('missing RPC endpoint');
    }
    if (!text.contains('://')) {
      if (!text.startsWith('[') && text.split(':').length == 2) {
        return text.split(':').first.toLowerCase();
      }
      if (text.startsWith('[')) {
        final end = text.indexOf(']');
        if (end > 1) return text.substring(1, end).toLowerCase();
      }
      return text.toLowerCase();
    }
    final uri = Uri.tryParse(text);
    final host = uri?.host ?? '';
    if (host.isEmpty) {
      throw AdditionRpcException('missing RPC host');
    }
    return host.toLowerCase();
  }

  static bool isLoopbackHost(String host) {
    final h = host.trim().toLowerCase();
    if (loopbackHosts.contains(h)) return true;
    if (h == '::1') return true;
    final parts = h.split('.');
    if (parts.length == 4) {
      final nums = parts.map(int.tryParse).toList();
      if (nums.every((n) => n != null && n >= 0 && n <= 255)) {
        return nums[0] == 127;
      }
    }
    return false;
  }

  static bool isRefusedPublicWriteHost(String host) {
    final h = host.trim().toLowerCase();
    if (refusedPublicWriteHosts.contains(h)) return true;
    if (h.endsWith('.additionblockchain.com')) return true;
    return false;
  }

  /// Desktop wallet write RPC is loopback-only (stricter than LAN).
  static String assertWriteEndpoint(String endpoint) {
    final host = hostFromEndpoint(endpoint);
    if (isRefusedPublicWriteHost(host) || !isLoopbackHost(host)) {
      throw AdditionRpcException(
        'write RPC refused: loopback only (127.0.0.1 / ::1 / localhost)',
      );
    }
    return host;
  }

  static bool isKnownPublicReadEndpoint(String endpoint) {
    final trimmed = endpoint.trim().replaceAll(RegExp(r'/+$'), '');
    if (knownPublicReadUrls.contains(trimmed) ||
        knownPublicReadUrls.contains(endpoint.trim())) {
      return true;
    }
    try {
      final host = hostFromEndpoint(endpoint);
      return host == 'rpc.additionblockchain.com' || host == '34.27.30.115';
    } catch (_) {
      return false;
    }
  }

  static String assertCommand(String command, {required bool write}) {
    if (command.contains('\n') || command.contains('\r')) {
      throw AdditionRpcException('RPC command must be a single line');
    }
    final token = firstCommandToken(command);
    if (token.isEmpty) {
      throw AdditionRpcException('empty RPC command');
    }
    final lowered = token.toLowerCase();
    if (insecureCommands.contains(lowered)) {
      throw AdditionRpcException('refusing insecure RPC command: $token');
    }
    final parts = lowered.split('_').toSet();
    if (foreignChainTokens.contains(lowered) ||
        parts.any(foreignChainTokens.contains) ||
        foreignChainTokens.any(lowered.contains)) {
      throw AdditionRpcException('ADDITION RPC only');
    }
    if (write) {
      if (!writeCommands.contains(lowered)) {
        throw AdditionRpcException('command not allowed on write RPC: $token');
      }
    } else if (!publicReadCommands.contains(lowered)) {
      throw AdditionRpcException('command disabled on public RPC: $token');
    }
    if (command.length > maxRpcLine) {
      throw AdditionRpcException(
        'RPC command exceeds $maxRpcLine-byte TEXT RPC limit',
      );
    }
    return token;
  }

  static int parseConfirmedBalance(String line) {
    final text = line.trim();
    if (text.isEmpty || text == 'RPC offline') {
      throw AdditionRpcOfflineException();
    }
    if (text.startsWith('error:')) {
      throw AdditionRpcException(text);
    }
    final values = parseKv(text);
    if (values.containsKey('confirmed')) {
      final raw = values['confirmed']!;
      if (!_isDigits(raw)) {
        throw AdditionRpcException(
          'RPC error: confirmed balance is not a whole unit',
        );
      }
      return int.parse(raw);
    }
    if (_isDigits(text)) return int.parse(text);
    throw AdditionRpcException(
      'RPC error: balance reply is not a whole-unit amount',
    );
  }

  static Map<String, String> parseGetinfo(String line) {
    final text = line.trim();
    if (text.isEmpty || text == 'RPC offline') {
      throw AdditionRpcOfflineException();
    }
    if (text.startsWith('error:')) {
      throw AdditionRpcException(text);
    }
    final values = parseKv(text);
    if (!values.containsKey('network')) {
      throw AdditionRpcException('RPC error: getinfo missing network');
    }
    if (values.containsKey('height') && !_isDigits(values['height']!)) {
      throw AdditionRpcException('RPC error: getinfo height is not an integer');
    }
    return values;
  }

  /// Honest network label from live getinfo only. Never invent stats.
  static String networkLabel(Map<String, String> info) {
    final network = (info['network'] ?? '').toLowerCase();
    final networkId = info['network_id'] ?? '';
    final networkName = info['network_name'] ?? '';
    if (networkId == productNetworkId ||
        networkName.toUpperCase().contains('ADDITION_MAINNET') ||
        network.contains('mainnet') ||
        network == 'main') {
      return productNetworkId;
    }
    if (network.contains('testnet') || network == 'test') {
      return 'testnet';
    }
    if (network.contains('regtest')) {
      return 'regtest';
    }
    if (network.isEmpty && networkId.isEmpty) return 'unknown network';
    if (networkId.isNotEmpty) return networkId;
    return '$network (from getinfo)';
  }

  /// Height / peers / last_tps only when present in live getinfo — never invent.
  static String liveStat(Map<String, String> info, String key) {
    final value = info[key];
    if (value == null || value.isEmpty) return '—';
    return value;
  }

  static bool _isDigits(String value) {
    if (value.isEmpty) return false;
    for (var i = 0; i < value.length; i++) {
      final c = value.codeUnitAt(i);
      if (c < 48 || c > 57) return false;
    }
    return true;
  }
}

class AdditionRpcException implements Exception {
  AdditionRpcException(this.message);
  final String message;

  @override
  String toString() => message;
}

class AdditionRpcOfflineException extends AdditionRpcException {
  AdditionRpcOfflineException([super.message = 'RPC offline']);
}
