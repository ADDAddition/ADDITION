import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../rpc/text_rpc_client.dart';
import '../rpc/wallet_client.dart';
import '../rpc/wallet_commands.dart';
import '../rpc/write_rpc_policy.dart';
import 'theme.dart';

class WalletScreen extends StatefulWidget {
  const WalletScreen({super.key, this.autoConnect = true});

  /// When false, skips the post-frame getinfo (useful in widget tests).
  final bool autoConnect;

  @override
  State<WalletScreen> createState() => _WalletScreenState();
}

class _WalletScreenState extends State<WalletScreen> {
  final _writeHost = TextEditingController(text: '127.0.0.1:8545');
  final _rpcToken = TextEditingController();
  final _walletName = TextEditingController(text: 'default');
  final _toAddr = TextEditingController();
  final _amount = TextEditingController(text: '1');
  final _fee = TextEditingController();

  String _status = 'Ready';
  String _networkLabel = '—';
  String _height = '—';
  String _peers = '—';
  String _address = '';
  String _balance = '—';
  String _walletList = '';
  String _lastReply = '';
  String _publicStatus = '';
  bool _busy = false;
  bool _showNode = false;

  WalletRpcClient _clientFor(String hostPort, String token) {
    final endpoint = WriteEndpoint.parse(hostPort, token: token.trim());
    return WalletRpcClient(TextRpcClient(endpoint: endpoint));
  }

  Future<void> _run(Future<void> Function() action) async {
    if (_busy) return;
    setState(() {
      _busy = true;
      _status = 'Working…';
    });
    try {
      await action();
    } on AdditionRpcOfflineException {
      setState(() {
        _status = 'RPC offline';
        _balance = 'unavailable';
      });
    } on AdditionRpcException catch (e) {
      setState(() => _status = e.message);
    } catch (e) {
      setState(() => _status = 'Error: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _refreshNode() async {
    await _run(() async {
      final client = _clientFor(_writeHost.text, _rpcToken.text);
      final info = await client.getinfo();
      setState(() {
        _networkLabel = WriteRpcPolicy.networkLabel(info);
        _height = info['height'] ?? '—';
        _peers = info['peers'] ?? '—';
        _lastReply = info.entries.map((e) => '${e.key}=${e.value}').join(' ');
        _status = 'Local write RPC answered';
      });
      final name = _walletName.text.trim();
      if (name.isNotEmpty) {
        try {
          final record = await client.walletInfo(name);
          final bal = await client.balance(name);
          setState(() {
            _address = record.address;
            _balance = '$bal ADD';
          });
        } on AdditionRpcException {
          // Wallet may not exist yet.
          setState(() {
            _address = '';
            _balance = '—';
          });
        }
      }
    });
  }

  Future<void> _createWallet() async {
    await _run(() async {
      final name = validateWalletName(_walletName.text.trim());
      final client = _clientFor(_writeHost.text, _rpcToken.text);
      final record = await client.createwallet(name: name);
      setState(() {
        _address = record.address;
        _lastReply =
            'name=${record.name} address=${record.address} algo=${record.algorithm} priv_printed=0';
        _status = 'Wallet created on node (keys stay in node wallets/*.wal)';
      });
      final bal = await client.balance(name);
      setState(() => _balance = '$bal ADD');
    });
  }

  Future<void> _listWallets() async {
    await _run(() async {
      final client = _clientFor(_writeHost.text, _rpcToken.text);
      final list = await client.walletList();
      setState(() {
        _walletList = list;
        _lastReply = list;
        _status = 'wallet_list ok';
      });
    });
  }

  Future<void> _loadWallet() async {
    await _run(() async {
      final name = validateWalletName(_walletName.text.trim());
      final client = _clientFor(_writeHost.text, _rpcToken.text);
      final record = await client.walletInfo(name);
      final bal = await client.balance(name);
      setState(() {
        _address = record.address;
        _balance = '$bal ADD';
        _lastReply =
            'name=${record.name} address=${record.address} algo=${record.algorithm}';
        _status = 'Wallet loaded';
      });
    });
  }

  Future<void> _send() async {
    await _run(() async {
      final name = validateWalletName(_walletName.text.trim());
      final to = validateAddress(_toAddr.text);
      final amount = parseWholeAmount(_amount.text);
      final fee = parseOptionalFee(_fee.text);
      final client = _clientFor(_writeHost.text, _rpcToken.text);
      final reply = await client.send(
        name: name,
        to: to,
        amount: amount,
        fee: fee,
      );
      setState(() {
        _lastReply = reply;
        _status = 'Send submitted (node signed; no private key on the wire)';
      });
      final bal = await client.balance(name);
      setState(() => _balance = '$bal ADD');
    });
  }

  Future<void> _publicGetinfo() async {
    await _run(() async {
      final line = await publicReadHttp('getinfo');
      final info = WriteRpcPolicy.parseGetinfo(line);
      setState(() {
        _publicStatus =
            '${WriteRpcPolicy.networkLabel(info)} · height=${info['height'] ?? '—'}';
        _lastReply = line;
        _status =
            'Public read getinfo (status only — not write RPC)';
      });
    });
  }

  @override
  void dispose() {
    _writeHost.dispose();
    _rpcToken.dispose();
    _walletName.dispose();
    _toAddr.dispose();
    _amount.dispose();
    _fee.dispose();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    if (widget.autoConnect) {
      WidgetsBinding.instance.addPostFrameCallback((_) => _refreshNode());
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('ADDITION Wallet'),
        actions: [
          IconButton(
            tooltip: 'Node settings',
            onPressed: () => setState(() => _showNode = !_showNode),
            icon: const Icon(Icons.settings),
          ),
        ],
      ),
      body: AbsorbPointer(
        absorbing: _busy,
        child: ListView(
          padding: const EdgeInsets.all(20),
          children: [
            _banner(),
            const SizedBox(height: 16),
            if (_showNode) ...[
              _nodeSettings(),
              const SizedBox(height: 16),
            ],
            _statusRow(),
            const SizedBox(height: 20),
            _balanceBlock(),
            const SizedBox(height: 20),
            _walletBlock(),
            const SizedBox(height: 20),
            _sendBlock(),
            const SizedBox(height: 20),
            _publicReadBlock(),
            const SizedBox(height: 20),
            _replyBlock(),
          ],
        ),
      ),
    );
  }

  Widget _banner() {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: AdditionTheme.panel,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AdditionTheme.line),
      ),
      child: const Text(
        'Desktop wallet for a node you control. Write RPC is loopback-only '
        '(127.0.0.1). Keys stay in the node wallets/*.wal store via createwallet / '
        'wallet_send — never printed or exported here. Not a token sale. '
        'Public product today is testnet; mainnet from getinfo is local/operator only.',
        style: TextStyle(color: AdditionTheme.mute, height: 1.35),
      ),
    );
  }

  Widget _statusRow() {
    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: [
        _kpi('Network', _networkLabel),
        _kpi('Height', _height),
        _kpi('Peers', _peers),
        _kpi('Balance', _balance),
      ],
    );
  }

  Widget _balanceBlock() {
    return _section(
      title: 'Receive',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SelectableText(
            _address.isEmpty ? 'No wallet loaded' : _address,
            style: const TextStyle(
              fontFamily: 'monospace',
              fontSize: 13,
              color: AdditionTheme.cream,
            ),
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              OutlinedButton.icon(
                onPressed: _address.isEmpty
                    ? null
                    : () async {
                        await Clipboard.setData(ClipboardData(text: _address));
                        setState(() => _status = 'Address copied');
                      },
                icon: const Icon(Icons.copy, size: 16),
                label: const Text('Copy address'),
              ),
              const SizedBox(width: 8),
              FilledButton(
                onPressed: _refreshNode,
                child: const Text('Refresh'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _walletBlock() {
    return _section(
      title: 'Wallet on node',
      child: Column(
        children: [
          TextField(
            controller: _walletName,
            decoration: const InputDecoration(
              labelText: 'Wallet name',
              hintText: 'default',
            ),
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              FilledButton(
                onPressed: _createWallet,
                child: const Text('Create wallet'),
              ),
              OutlinedButton(
                onPressed: _loadWallet,
                child: const Text('Load / info'),
              ),
              OutlinedButton(
                onPressed: _listWallets,
                child: const Text('List wallets'),
              ),
            ],
          ),
          if (_walletList.isNotEmpty) ...[
            const SizedBox(height: 12),
            SelectableText(
              _walletList,
              style: const TextStyle(
                fontFamily: 'monospace',
                fontSize: 12,
                color: AdditionTheme.mute,
              ),
            ),
          ],
        ],
      ),
    );
  }

  Widget _sendBlock() {
    return _section(
      title: 'Send (wallet_send — node signs)',
      child: Column(
        children: [
          TextField(
            controller: _toAddr,
            decoration: const InputDecoration(
              labelText: 'To address (128 hex)',
            ),
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _amount,
                  decoration: const InputDecoration(
                    labelText: 'Amount (whole ADD)',
                  ),
                  keyboardType: TextInputType.number,
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: TextField(
                  controller: _fee,
                  decoration: const InputDecoration(
                    labelText: 'Fee (blank = node floor)',
                  ),
                  keyboardType: TextInputType.number,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton(
              onPressed: _address.isEmpty ? null : _send,
              child: const Text('Send'),
            ),
          ),
        ],
      ),
    );
  }

  Widget _publicReadBlock() {
    return _section(
      title: 'Public read (status only)',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'Optional getinfo via https://rpc.additionblockchain.com/rpc?cmd=getinfo. '
            'Cannot createwallet or send.',
            style: TextStyle(color: AdditionTheme.mute, fontSize: 13),
          ),
          const SizedBox(height: 8),
          OutlinedButton(
            onPressed: _publicGetinfo,
            child: const Text('Public getinfo'),
          ),
          if (_publicStatus.isNotEmpty) ...[
            const SizedBox(height: 8),
            Text(
              _publicStatus,
              style: const TextStyle(color: AdditionTheme.cream),
            ),
          ],
        ],
      ),
    );
  }

  Widget _nodeSettings() {
    return _section(
      title: 'Write RPC (loopback only)',
      child: Column(
        children: [
          TextField(
            controller: _writeHost,
            decoration: const InputDecoration(
              labelText: 'Host:port',
              hintText: '127.0.0.1:8545',
            ),
          ),
          const SizedBox(height: 8),
          TextField(
            controller: _rpcToken,
            decoration: const InputDecoration(
              labelText: 'Optional ADDITION_RPC_TOKEN',
            ),
            obscureText: true,
          ),
          const SizedBox(height: 8),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton(
              onPressed: _refreshNode,
              child: const Text('Connect / getinfo'),
            ),
          ),
        ],
      ),
    );
  }

  Widget _replyBlock() {
    return _section(
      title: 'Status',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(_status, style: const TextStyle(color: AdditionTheme.cream)),
          if (_lastReply.isNotEmpty) ...[
            const SizedBox(height: 8),
            SelectableText(
              _lastReply,
              style: const TextStyle(
                fontFamily: 'monospace',
                fontSize: 12,
                color: AdditionTheme.mute,
              ),
            ),
          ],
        ],
      ),
    );
  }

  Widget _section({required String title, required Widget child}) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AdditionTheme.panel,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AdditionTheme.line),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: const TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w600,
              color: AdditionTheme.cream,
            ),
          ),
          const SizedBox(height: 12),
          child,
        ],
      ),
    );
  }

  Widget _kpi(String label, String value) {
    return Container(
      constraints: const BoxConstraints(minWidth: 140),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AdditionTheme.panelLift,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AdditionTheme.line),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: AdditionTheme.mute)),
          const SizedBox(height: 4),
          Text(
            value,
            style: const TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w600,
              color: AdditionTheme.cream,
            ),
          ),
        ],
      ),
    );
  }
}
