import 'package:flutter/foundation.dart';

import '../rpc/text_rpc_client.dart';
import '../rpc/wallet_client.dart';
import '../rpc/wallet_commands.dart';
import '../rpc/write_rpc_policy.dart';

/// Shared node/wallet state for the Addition Core shell.
class CoreSession extends ChangeNotifier {
  CoreSession({
    this.writeHost =
        '${WriteRpcPolicy.defaultHost}:${WriteRpcPolicy.defaultPort}',
    this.walletName = 'default',
  });

  String writeHost;
  String rpcToken = '';
  String walletName;

  String status = 'Ready';
  String networkLabel = '—';
  String height = '—';
  String peersCount = '—';
  String lastTps = '—';
  String nextReward = '—';
  String address = '';
  String balance = '—';
  String walletList = '';
  String lastReply = '';
  String peersDetail = '';
  String publicStatus = '';
  bool busy = false;
  final List<String> consoleLog = [];

  WalletRpcClient clientFor() {
    final endpoint = WriteEndpoint.parse(writeHost, token: rpcToken.trim());
    return WalletRpcClient(TextRpcClient(endpoint: endpoint));
  }

  Future<void> run(Future<void> Function() action) async {
    if (busy) return;
    busy = true;
    status = 'Working…';
    notifyListeners();
    try {
      await action();
    } on AdditionRpcOfflineException {
      status = 'RPC offline';
      balance = 'unavailable';
    } on AdditionRpcException catch (e) {
      status = e.message;
    } catch (e) {
      status = 'Error: $e';
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<void> refreshNode() async {
    await run(() async {
      final client = clientFor();
      final info = await client.getinfo();
      networkLabel = WriteRpcPolicy.networkLabel(info);
      height = WriteRpcPolicy.liveStat(info, 'height');
      peersCount = WriteRpcPolicy.liveStat(info, 'peers');
      lastTps = WriteRpcPolicy.liveStat(info, 'last_tps');
      nextReward = WriteRpcPolicy.liveStat(info, 'next_reward');
      lastReply = info.entries.map((e) => '${e.key}=${e.value}').join(' ');
      status = 'Local write RPC answered ($writeHost)';
      final name = walletName.trim();
      if (name.isNotEmpty) {
        try {
          final record = await client.walletInfo(name);
          final bal = await client.balance(name);
          address = record.address;
          balance = '$bal ADD';
        } on AdditionRpcException {
          address = '';
          balance = '—';
        }
      }
    });
  }

  Future<void> createWallet() async {
    await run(() async {
      final name = validateWalletName(walletName.trim());
      final client = clientFor();
      final record = await client.createwallet(name: name);
      address = record.address;
      lastReply =
          'name=${record.name} address=${record.address} algo=${record.algorithm} priv_printed=0';
      status = 'Wallet created on node (keys stay in node wallets/*.wal)';
      final bal = await client.balance(name);
      balance = '$bal ADD';
    });
  }

  Future<void> loadWallet() async {
    await run(() async {
      final name = validateWalletName(walletName.trim());
      final client = clientFor();
      final record = await client.walletInfo(name);
      final bal = await client.balance(name);
      address = record.address;
      balance = '$bal ADD';
      lastReply =
          'name=${record.name} address=${record.address} algo=${record.algorithm}';
      status = 'Wallet loaded';
    });
  }

  Future<void> listWallets() async {
    await run(() async {
      final client = clientFor();
      final list = await client.walletList();
      walletList = list;
      lastReply = list;
      status = 'wallet_list ok';
    });
  }

  Future<void> send({
    required String to,
    required String amountText,
    String feeText = '',
  }) async {
    await run(() async {
      final name = validateWalletName(walletName.trim());
      final toAddr = validateAddress(to);
      final amount = parseWholeAmount(amountText);
      final fee = parseOptionalFee(feeText);
      final client = clientFor();
      final reply = await client.send(
        name: name,
        to: toAddr,
        amount: amount,
        fee: fee,
      );
      lastReply = reply;
      status = 'Send submitted (node signed; loopback write only)';
      final bal = await client.balance(name);
      balance = '$bal ADD';
    });
  }

  Future<void> mine({int? threads}) async {
    await run(() async {
      if (address.isEmpty) {
        throw AdditionRpcException('load or create a wallet first');
      }
      final client = clientFor();
      final reply = await client.mine(address, threads: threads);
      lastReply = reply;
      status =
          'Mine submitted (local trusted RPC; coinbase ${WriteRpcPolicy.coinbaseReward} ADD, 100% finding miner)';
      final info = await client.getinfo();
      height = WriteRpcPolicy.liveStat(info, 'height');
      peersCount = WriteRpcPolicy.liveStat(info, 'peers');
      lastTps = WriteRpcPolicy.liveStat(info, 'last_tps');
      nextReward = WriteRpcPolicy.liveStat(info, 'next_reward');
      final bal = await client.balance(walletName.trim());
      balance = '$bal ADD';
    });
  }

  Future<void> refreshPeers() async {
    await run(() async {
      final client = clientFor();
      final info = await client.getinfo();
      peersCount = WriteRpcPolicy.liveStat(info, 'peers');
      networkLabel = WriteRpcPolicy.networkLabel(info);
      height = WriteRpcPolicy.liveStat(info, 'height');
      final detail = await client.peers();
      peersDetail = detail.isEmpty ? '(no peers listed)' : detail;
      lastReply = 'peers=$peersCount detail=$peersDetail';
      status = 'Peers from live node getinfo/peers';
    });
  }

  Future<void> console(String command) async {
    final line = command.trim();
    if (line.isEmpty) return;
    await run(() async {
      final client = clientFor();
      final reply = await client.console(line);
      consoleLog.add('> $line');
      consoleLog.add(reply);
      if (consoleLog.length > 200) {
        consoleLog.removeRange(0, consoleLog.length - 200);
      }
      lastReply = reply;
      status = 'Console RPC ok';
    });
  }

  Future<void> publicGetinfo() async {
    await run(() async {
      final line = await publicReadHttp('getinfo');
      final info = WriteRpcPolicy.parseGetinfo(line);
      publicStatus =
          '${WriteRpcPolicy.networkLabel(info)} · height=${WriteRpcPolicy.liveStat(info, 'height')} · peers=${WriteRpcPolicy.liveStat(info, 'peers')}';
      lastReply = line;
      status =
          'Public read getinfo (status only — wallet_send stays off public port)';
    });
  }
}
