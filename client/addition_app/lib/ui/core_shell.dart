import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../rpc/write_rpc_policy.dart';
import 'banners.dart';
import 'core_session.dart';
import 'theme.dart';

enum CoreNav { wallet, receive, send, mine, peers, console }

/// Bitcoin Core-style single-window full-node operator GUI.
class CoreShell extends StatefulWidget {
  const CoreShell({
    super.key,
    this.autoConnect = true,
    this.enableVideo = true,
  });

  final bool autoConnect;
  final bool enableVideo;

  @override
  State<CoreShell> createState() => _CoreShellState();
}

class _CoreShellState extends State<CoreShell> {
  late final CoreSession _session;
  CoreNav _nav = CoreNav.wallet;
  bool _showNode = false;

  final _writeHost = TextEditingController(
    text: '${WriteRpcPolicy.defaultHost}:${WriteRpcPolicy.defaultPort}',
  );
  final _rpcToken = TextEditingController();
  final _walletName = TextEditingController(text: 'default');
  final _toAddr = TextEditingController();
  final _amount = TextEditingController(text: '1');
  final _fee = TextEditingController();
  final _console = TextEditingController();
  final _mineThreads = TextEditingController();

  @override
  void initState() {
    super.initState();
    _session = CoreSession();
    _session.addListener(_onSession);
    if (widget.autoConnect) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        _syncFields();
        _session.refreshNode();
      });
    }
  }

  void _onSession() {
    if (mounted) setState(() {});
  }

  void _syncFields() {
    _session.writeHost = _writeHost.text.trim();
    _session.rpcToken = _rpcToken.text;
    _session.walletName = _walletName.text.trim();
  }

  @override
  void dispose() {
    _session.removeListener(_onSession);
    _session.dispose();
    _writeHost.dispose();
    _rpcToken.dispose();
    _walletName.dispose();
    _toAddr.dispose();
    _amount.dispose();
    _fee.dispose();
    _console.dispose();
    _mineThreads.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = _session;
    return Scaffold(
      backgroundColor: AdditionTheme.ink,
      body: AbsorbPointer(
        absorbing: s.busy,
        child: Column(
          children: [
            _titleBar(),
            CoreBannerBand(enableVideo: widget.enableVideo),
            Expanded(
              child: CoreBannerBackground(
                enableVideo: false, // band owns the muted player; avoid dual play
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    _navRail(),
                    const VerticalDivider(width: 1, color: AdditionTheme.line),
                    Expanded(child: _pageBody()),
                  ],
                ),
              ),
            ),
            _statusBar(),
          ],
        ),
      ),
    );
  }

  Widget _titleBar() {
    return Material(
      color: AdditionTheme.panel,
      child: SafeArea(
        bottom: false,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
          child: Row(
            children: [
              const Text(
                'ADDITION',
                style: TextStyle(
                  color: AdditionTheme.logoRed,
                  fontWeight: FontWeight.w800,
                  fontSize: 18,
                  letterSpacing: 1.2,
                ),
              ),
              const SizedBox(width: 10),
              const Text(
                'Core',
                style: TextStyle(
                  color: AdditionTheme.cream,
                  fontWeight: FontWeight.w600,
                  fontSize: 18,
                ),
              ),
              const SizedBox(width: 12),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                decoration: BoxDecoration(
                  border: Border.all(color: AdditionTheme.line),
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  WriteRpcPolicy.productNetworkId,
                  style: const TextStyle(
                    color: AdditionTheme.mute,
                    fontSize: 11,
                    fontFamily: 'monospace',
                  ),
                ),
              ),
              const Spacer(),
              IconButton(
                tooltip: 'Node settings',
                onPressed: () => setState(() => _showNode = !_showNode),
                icon: const Icon(Icons.settings, color: AdditionTheme.mute),
              ),
              IconButton(
                tooltip: 'Refresh getinfo',
                onPressed: () {
                  _syncFields();
                  _session.refreshNode();
                },
                icon: const Icon(Icons.refresh, color: AdditionTheme.cream),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _navRail() {
    Widget item(CoreNav id, IconData icon, String label) {
      final selected = _nav == id;
      return InkWell(
        onTap: () => setState(() => _nav = id),
        child: Container(
          width: 148,
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
          color: selected ? AdditionTheme.panelLift : Colors.transparent,
          child: Row(
            children: [
              Icon(
                icon,
                size: 18,
                color: selected ? AdditionTheme.logoRed : AdditionTheme.mute,
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  label,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: selected ? AdditionTheme.cream : AdditionTheme.mute,
                    fontWeight: selected ? FontWeight.w600 : FontWeight.w400,
                  ),
                ),
              ),
            ],
          ),
        ),
      );
    }

    return ColoredBox(
      color: AdditionTheme.panel,
      child: SizedBox(
        width: 148,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const SizedBox(height: 8),
            item(CoreNav.wallet, Icons.account_balance_wallet, 'Wallet'),
            item(CoreNav.receive, Icons.call_received, 'Receive'),
            item(CoreNav.send, Icons.call_made, 'Send'),
            item(CoreNav.mine, Icons.hardware, 'Mine'),
            item(CoreNav.peers, Icons.hub, 'Peers'),
            item(CoreNav.console, Icons.terminal, 'Console'),
            const Spacer(),
            const Padding(
              padding: EdgeInsets.all(12),
              child: Text(
                'Full node GUI\nloopback write only',
                style: TextStyle(color: AdditionTheme.mute, fontSize: 11, height: 1.3),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _pageBody() {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        if (_showNode) ...[
          _nodeSettings(),
          const SizedBox(height: 16),
        ],
        switch (_nav) {
          CoreNav.wallet => _walletPage(),
          CoreNav.receive => _receivePage(),
          CoreNav.send => _sendPage(),
          CoreNav.mine => _minePage(),
          CoreNav.peers => _peersPage(),
          CoreNav.console => _consolePage(),
        },
      ],
    );
  }

  Widget _walletPage() {
    final s = _session;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _section(
          title: 'Wallet / balance',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _kpi('Network', s.networkLabel),
                  _kpi('Height', s.height),
                  _kpi('Peers', s.peersCount),
                  _kpi('Balance', s.balance),
                  _kpi('last_tps', s.lastTps),
                  _kpi('next_reward', s.nextReward),
                ],
              ),
              const SizedBox(height: 12),
              const Text(
                'Height / peers / last_tps / next_reward come from live getinfo only. '
                'Coinbase is 50 ADD (100% to finding miner) — no invented staker cut.',
                style: TextStyle(color: AdditionTheme.mute, fontSize: 13, height: 1.35),
              ),
              const SizedBox(height: 12),
              TextField(
                controller: _walletName,
                decoration: const InputDecoration(
                  labelText: 'Wallet name',
                  hintText: 'default',
                ),
                onChanged: (_) => _syncFields(),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  FilledButton(
                    onPressed: () {
                      _syncFields();
                      _session.createWallet();
                    },
                    child: const Text('Create wallet'),
                  ),
                  OutlinedButton(
                    onPressed: () {
                      _syncFields();
                      _session.loadWallet();
                    },
                    child: const Text('Load / info'),
                  ),
                  OutlinedButton(
                    onPressed: () {
                      _syncFields();
                      _session.listWallets();
                    },
                    child: const Text('List wallets'),
                  ),
                ],
              ),
              if (s.walletList.isNotEmpty) ...[
                const SizedBox(height: 12),
                SelectableText(
                  s.walletList,
                  style: const TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 12,
                    color: AdditionTheme.mute,
                  ),
                ),
              ],
              const SizedBox(height: 16),
              _section(
                title: 'Public read (status only)',
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Optional getinfo via site /api/rpc → 34.27.30.115:38546. '
                      'Cannot createwallet or wallet_send.',
                      style: TextStyle(color: AdditionTheme.mute, fontSize: 13),
                    ),
                    const SizedBox(height: 8),
                    OutlinedButton(
                      onPressed: () => _session.publicGetinfo(),
                      child: const Text('Public getinfo'),
                    ),
                    if (s.publicStatus.isNotEmpty) ...[
                      const SizedBox(height: 8),
                      Text(
                        s.publicStatus,
                        style: const TextStyle(color: AdditionTheme.cream),
                      ),
                    ],
                  ],
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _replyBlock(),
      ],
    );
  }

  Widget _receivePage() {
    final s = _session;
    return _section(
      title: 'Receive',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SelectableText(
            s.address.isEmpty ? 'No wallet loaded' : s.address,
            style: const TextStyle(
              fontFamily: 'monospace',
              fontSize: 13,
              color: AdditionTheme.cream,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            'Balance: ${s.balance}',
            style: const TextStyle(color: AdditionTheme.mute),
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              OutlinedButton.icon(
                onPressed: s.address.isEmpty
                    ? null
                    : () async {
                        await Clipboard.setData(ClipboardData(text: s.address));
                        setState(() => _session.status = 'Address copied');
                      },
                icon: const Icon(Icons.copy, size: 16),
                label: const Text('Copy address'),
              ),
              const SizedBox(width: 8),
              FilledButton(
                onPressed: () {
                  _syncFields();
                  _session.refreshNode();
                },
                child: const Text('Refresh'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _sendPage() {
    final s = _session;
    return Column(
      children: [
        _section(
          title: 'Send (wallet_send — loopback write only)',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                'Refuses non-loopback hosts. Public 38546 has no wallet_send. '
                'Node signs; keys stay in wallets/*.wal.',
                style: TextStyle(color: AdditionTheme.mute, fontSize: 13, height: 1.35),
              ),
              const SizedBox(height: 12),
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
              FilledButton(
                onPressed: s.address.isEmpty
                    ? null
                    : () {
                        _syncFields();
                        _session.send(
                          to: _toAddr.text,
                          amountText: _amount.text,
                          feeText: _fee.text,
                        );
                      },
                child: const Text('Send'),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _replyBlock(),
      ],
    );
  }

  Widget _minePage() {
    final s = _session;
    return Column(
      children: [
        _section(
          title: 'Mine (local trusted RPC)',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Reward address: ${s.address.isEmpty ? '(load a wallet)' : s.address}',
                style: const TextStyle(
                  fontFamily: 'monospace',
                  fontSize: 12,
                  color: AdditionTheme.cream,
                ),
              ),
              const SizedBox(height: 8),
              Text(
                'Coinbase ${WriteRpcPolicy.coinbaseReward} ADD — 100% to the finding miner. '
                'Uses local loopback write RPC. Public 38546 may already allow mine; '
                'this GUI never opens send/keys on 0.0.0.0.',
                style: const TextStyle(color: AdditionTheme.mute, fontSize: 13, height: 1.35),
              ),
              const SizedBox(height: 12),
              TextField(
                controller: _mineThreads,
                decoration: const InputDecoration(
                  labelText: 'Threads (blank = node default)',
                ),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 12),
              FilledButton(
                onPressed: s.address.isEmpty
                    ? null
                    : () {
                        _syncFields();
                        final raw = _mineThreads.text.trim();
                        final threads = raw.isEmpty ? null : int.tryParse(raw);
                        _session.mine(threads: threads);
                      },
                child: const Text('Mine one block'),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _kpi('Height', s.height),
                  _kpi('next_reward', s.nextReward),
                  _kpi('last_tps', s.lastTps),
                  _kpi('Balance', s.balance),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _replyBlock(),
      ],
    );
  }

  Widget _peersPage() {
    final s = _session;
    return Column(
      children: [
        _section(
          title: 'Peers',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _kpi('Network', s.networkLabel),
                  _kpi('Height', s.height),
                  _kpi('Peers (getinfo)', s.peersCount),
                ],
              ),
              const SizedBox(height: 12),
              const Text(
                'Peer count and list come from the live node (getinfo / peers). '
                'No invented numbers.',
                style: TextStyle(color: AdditionTheme.mute, fontSize: 13),
              ),
              const SizedBox(height: 12),
              FilledButton(
                onPressed: () {
                  _syncFields();
                  _session.refreshPeers();
                },
                child: const Text('Refresh peers'),
              ),
              const SizedBox(height: 12),
              SelectableText(
                s.peersDetail.isEmpty ? '—' : s.peersDetail,
                style: const TextStyle(
                  fontFamily: 'monospace',
                  fontSize: 12,
                  color: AdditionTheme.cream,
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        _replyBlock(),
      ],
    );
  }

  Widget _consolePage() {
    final s = _session;
    return _section(
      title: 'Console (TEXT RPC → local node)',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'Issue one TEXT RPC line to loopback additiond. '
            'Insecure sendtx/sign_message and foreign-chain commands are refused.',
            style: TextStyle(color: AdditionTheme.mute, fontSize: 13, height: 1.35),
          ),
          const SizedBox(height: 12),
          Container(
            width: double.infinity,
            height: 280,
            padding: const EdgeInsets.all(10),
            decoration: BoxDecoration(
              color: AdditionTheme.ink,
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: AdditionTheme.line),
            ),
            child: SingleChildScrollView(
              reverse: true,
              child: SelectableText(
                s.consoleLog.isEmpty
                    ? 'Ready. Example: getinfo'
                    : s.consoleLog.join('\n'),
                style: const TextStyle(
                  fontFamily: 'monospace',
                  fontSize: 12,
                  color: AdditionTheme.cream,
                  height: 1.4,
                ),
              ),
            ),
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _console,
                  decoration: const InputDecoration(
                    labelText: 'RPC command',
                    hintText: 'getinfo',
                  ),
                  onSubmitted: (_) => _runConsole(),
                ),
              ),
              const SizedBox(width: 8),
              FilledButton(
                onPressed: _runConsole,
                child: const Text('Send'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  void _runConsole() {
    _syncFields();
    final line = _console.text;
    _console.clear();
    _session.console(line);
  }

  Widget _nodeSettings() {
    return _section(
      title: 'Write RPC (loopback only — never 0.0.0.0)',
      child: Column(
        children: [
          TextField(
            controller: _writeHost,
            decoration: const InputDecoration(
              labelText: 'Host:port',
              hintText: '127.0.0.1:8546',
            ),
            onChanged: (_) => _syncFields(),
          ),
          const SizedBox(height: 8),
          TextField(
            controller: _rpcToken,
            decoration: const InputDecoration(
              labelText: 'Optional ADDITION_RPC_TOKEN',
            ),
            obscureText: true,
            onChanged: (_) => _syncFields(),
          ),
          const SizedBox(height: 8),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton(
              onPressed: () {
                _syncFields();
                _session.refreshNode();
              },
              child: const Text('Connect / getinfo'),
            ),
          ),
        ],
      ),
    );
  }

  Widget _replyBlock() {
    final s = _session;
    return _section(
      title: 'Status',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(s.status, style: const TextStyle(color: AdditionTheme.cream)),
          if (s.lastReply.isNotEmpty) ...[
            const SizedBox(height: 8),
            SelectableText(
              s.lastReply,
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

  Widget _statusBar() {
    final s = _session;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
      color: AdditionTheme.panelLift,
      child: Text(
        '${s.networkLabel}  ·  height ${s.height}  ·  peers ${s.peersCount}  ·  '
        '${s.busy ? 'busy' : s.status}  ·  write ${s.writeHost}',
        style: const TextStyle(
          color: AdditionTheme.mute,
          fontSize: 12,
          fontFamily: 'monospace',
        ),
        overflow: TextOverflow.ellipsis,
      ),
    );
  }

  Widget _section({required String title, required Widget child}) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AdditionTheme.panel,
        borderRadius: BorderRadius.circular(10),
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
      constraints: const BoxConstraints(minWidth: 120),
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: AdditionTheme.panelLift,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AdditionTheme.line),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: AdditionTheme.mute, fontSize: 12)),
          const SizedBox(height: 4),
          Text(
            value,
            style: const TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w600,
              color: AdditionTheme.cream,
            ),
          ),
        ],
      ),
    );
  }
}
