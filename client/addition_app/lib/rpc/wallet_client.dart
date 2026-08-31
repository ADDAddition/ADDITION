import 'kv_parser.dart';
import 'text_rpc_client.dart';
import 'wallet_commands.dart';
import 'write_rpc_policy.dart';

class WalletRecord {
  WalletRecord({
    required this.name,
    required this.address,
    this.algorithm = '',
    this.publicKey = '',
  });

  final String name;
  final String address;
  final String algorithm;
  final String publicKey;
}

class WalletRpcClient {
  WalletRpcClient(this.rpc);

  final TextRpcClient rpc;

  Future<WalletRecord> createwallet({String name = 'default'}) async {
    final line = await rpc.call(buildCreatewallet(name));
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    final values = parseKv(line);
    _assertNoPrivateKeyMaterial(values);
    final address = values['address'];
    if (address == null) {
      throw AdditionRpcException('RPC error: createwallet missing address');
    }
    return WalletRecord(
      name: values['name'] ?? name,
      address: validateAddress(address),
      algorithm: values['algo'] ?? '',
      publicKey: values['pub'] ?? '',
    );
  }

  Future<WalletRecord> walletInfo(String name) async {
    final line = await rpc.call(buildWalletInfo(name));
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    final values = parseKv(line);
    _assertNoPrivateKeyMaterial(values);
    final address = values['address'];
    if (address == null) {
      throw AdditionRpcException('RPC error: wallet_info missing address');
    }
    return WalletRecord(
      name: values['name'] ?? name,
      address: validateAddress(address),
      algorithm: values['algo'] ?? '',
      publicKey: values['pub'] ?? '',
    );
  }

  Future<String> walletList() async {
    final line = await rpc.call(buildWalletList());
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    return line;
  }

  Future<int> balance(String name) async {
    final line = await rpc.call(buildWalletBalance(name));
    return WriteRpcPolicy.parseConfirmedBalance(line);
  }

  Future<int> getbalance(String address) async {
    final line = await rpc.call(buildGetbalance(address));
    return WriteRpcPolicy.parseConfirmedBalance(line);
  }

  Future<String> send({
    required String name,
    required String to,
    required int amount,
    int? fee,
  }) async {
    final info = await walletInfo(name);
    if (to == info.address) {
      throw AdditionRpcException('refusing to send to the same address');
    }
    final line = await rpc.call(
      buildWalletSend(name, to, amount, fee: fee),
    );
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    if (!line.contains('ok:gossiped') && !line.contains('hash=')) {
      throw AdditionRpcException('RPC error: wallet_send did not confirm');
    }
    // Defense in depth: ensure no private key leaked onto the wire.
    for (final wire in rpc.sent) {
      if (wire.toLowerCase().contains('priv') &&
          wire.toLowerCase().contains('key')) {
        throw AdditionRpcException('private key leaked onto the TEXT RPC line');
      }
      final first = firstCommandToken(wire);
      if (WriteRpcPolicy.insecureCommands.contains(first.toLowerCase())) {
        throw AdditionRpcException('refusing insecure RPC command: $first');
      }
    }
    return line;
  }

  Future<Map<String, String>> getinfo() async {
    return WriteRpcPolicy.parseGetinfo(await rpc.call(buildGetinfo()));
  }

  Future<Map<String, String>> feeInfo() async {
    final line = await rpc.call(buildFeeInfo());
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    return parseKv(line);
  }

  /// Local trusted mine. Coinbase is 50 ADD, 100% to finding miner.
  Future<String> mine(String address, {int? threads}) async {
    final line = await rpc.call(buildMine(address, threads: threads));
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    return line;
  }

  /// Honest peer list from the node (`peers` TEXT RPC).
  Future<String> peers() async {
    final line = await rpc.call(buildPeers());
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    return line;
  }

  Future<Map<String, String>> monetaryInfo() async {
    final line = await rpc.call(buildMonetaryInfo());
    if (line.startsWith('error:')) throw AdditionRpcException(line);
    return parseKv(line);
  }

  /// Console: issue one TEXT RPC line to the local loopback node.
  Future<String> console(String command) async {
    return rpc.consoleCall(command);
  }

  /// Allow `priv_printed=0` (honest node flag). Refuse actual key fields.
  static void _assertNoPrivateKeyMaterial(Map<String, String> values) {
    const allowedFlags = {'priv_printed'};
    const forbidden = {
      'priv',
      'private_key',
      'privkey',
      'secret',
      'sk',
      'secret_key',
    };
    for (final entry in values.entries) {
      final key = entry.key.toLowerCase();
      if (allowedFlags.contains(key)) {
        if (entry.value != '0' && entry.value.toLowerCase() != 'false') {
          throw AdditionRpcException(
            'refusing reply that printed private key material',
          );
        }
        continue;
      }
      if (forbidden.contains(key) ||
          (key.contains('priv') && key.contains('key')) ||
          key == 'sk') {
        throw AdditionRpcException(
          'refusing reply that appears to include private key material',
        );
      }
    }
  }
}
