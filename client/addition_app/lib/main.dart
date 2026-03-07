import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

void main() {
  runApp(const AdditionApp());
}

class AdditionApp extends StatelessWidget {
  const AdditionApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Addition Multi-Platform',
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFFFF3B3B), brightness: Brightness.dark),
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _apiBase = TextEditingController(text: 'http://127.0.0.1:8080');
  final _tokenIn = TextEditingController(text: 'USDT');
  final _tokenOut = TextEditingController(text: 'ADD');
  final _amountIn = TextEditingController(text: '100');
  final _maxHops = TextEditingController(text: '3');
  final _trader = TextEditingController(text: 'wallet1');
  final _minOut = TextEditingController(text: '0');
  final _deadlineSec = TextEditingController(text: '60');
  final _traderPub = TextEditingController();
  final _traderSig = TextEditingController();
  final _signPayload = TextEditingController();

  String _health = 'Unknown';
  String _height = '-';
  String _mempool = '-';
  String _bestRoute = '-';
  String _bestAmountOut = '-';
  String _status = 'Ready';

  Uri _u(String path, [Map<String, String>? q]) {
    final base = Uri.parse(_apiBase.text.trim());
    return base.replace(path: path, queryParameters: q);
  }

  Future<Map<String, dynamic>> _getJson(Uri uri) async {
    final res = await http.get(uri).timeout(const Duration(seconds: 8));
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  Future<void> _refresh() async {
    setState(() => _status = 'Refreshing...');
    try {
      final health = await _getJson(_u('/api/health'));
      final info = await _getJson(_u('/api/getinfo'));
      final data = (info['data'] as Map?)?.cast<String, dynamic>() ?? {};
      setState(() {
        _health = health['ok'] == true ? 'ONLINE' : 'OFFLINE';
        _height = '${data['height'] ?? '-'}';
        _mempool = '${data['mempool'] ?? '-'}';
        _status = 'Updated';
      });
    } catch (e) {
      setState(() => _status = 'Error: $e');
    }
  }

  Future<void> _quoteBestRoute() async {
    setState(() => _status = 'Quoting best route...');
    try {
      final json = await _getJson(_u('/api/swap_best_route', {
        'token_in': _tokenIn.text.trim(),
        'token_out': _tokenOut.text.trim(),
        'amount_in': _amountIn.text.trim(),
        'max_hops': _maxHops.text.trim(),
      }));
      if (json['ok'] != true) {
        setState(() => _status = 'Error: ${json['error']}');
        return;
      }
      final data = (json['data'] as Map?)?.cast<String, dynamic>() ?? {};
      setState(() {
        _bestAmountOut = '${data['amount_out'] ?? '-'}';
        _bestRoute = '${data['route'] ?? '-'}';
        _status = 'Best route ready';
      });
    } catch (e) {
      setState(() => _status = 'Error: $e');
    }
  }

  int _deadlineUnixNowPlus() {
    final sec = int.tryParse(_deadlineSec.text.trim()) ?? 60;
    final safeSec = sec < 5 ? 5 : sec;
    return DateTime.now().millisecondsSinceEpoch ~/ 1000 + safeSec;
  }

  Future<void> _getSignPayload() async {
    setState(() => _status = 'Building sign payload...');
    try {
      final json = await _getJson(_u('/api/swap_best_route_sign_payload', {
        'token_in': _tokenIn.text.trim(),
        'token_out': _tokenOut.text.trim(),
        'trader': _trader.text.trim(),
        'amount_in': _amountIn.text.trim(),
        'min_out': _minOut.text.trim(),
        'deadline_unix': '${_deadlineUnixNowPlus()}',
        'max_hops': _maxHops.text.trim(),
      }));
      if (json['ok'] != true) {
        setState(() => _status = 'Error: ${json['error']}');
        return;
      }
      _signPayload.text = '${json['payload'] ?? ''}';
      setState(() => _status = 'Payload ready to sign');
    } catch (e) {
      setState(() => _status = 'Error: $e');
    }
  }

  Future<void> _executeSignedBestRoute() async {
    setState(() => _status = 'Executing signed best-route...');
    try {
      final json = await _getJson(_u('/api/swap_best_route_exact_in_signed', {
        'token_in': _tokenIn.text.trim(),
        'token_out': _tokenOut.text.trim(),
        'trader': _trader.text.trim(),
        'amount_in': _amountIn.text.trim(),
        'min_out': _minOut.text.trim(),
        'deadline_unix': '${_deadlineUnixNowPlus()}',
        'max_hops': _maxHops.text.trim(),
        'trader_pubkey': _traderPub.text.trim(),
        'trader_sig': _traderSig.text.trim(),
      }));
      if (json['ok'] != true) {
        setState(() => _status = 'Error: ${json['error']}');
        return;
      }
      final data = (json['data'] as Map?)?.cast<String, dynamic>() ?? {};
      setState(() {
        _bestAmountOut = '${data['amount_out'] ?? '-'}';
        _bestRoute = '${data['route'] ?? '-'}';
        _status = 'Signed swap executed';
      });
    } catch (e) {
      setState(() => _status = 'Error: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Addition Client • Windows/macOS/Web/Mobile')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          TextField(controller: _apiBase, decoration: const InputDecoration(labelText: 'Portal API Base URL')),
          const SizedBox(height: 12),
          Row(
            children: [
              Expanded(child: _kpi('Health', _health)),
              const SizedBox(width: 8),
              Expanded(child: _kpi('Height', _height)),
              const SizedBox(width: 8),
              Expanded(child: _kpi('Mempool', _mempool)),
            ],
          ),
          const SizedBox(height: 16),
          const Text('Best Route Swap Quote', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
          const SizedBox(height: 8),
          TextField(controller: _tokenIn, decoration: const InputDecoration(labelText: 'Token In')),
          TextField(controller: _tokenOut, decoration: const InputDecoration(labelText: 'Token Out')),
          TextField(controller: _trader, decoration: const InputDecoration(labelText: 'Trader Address')),
          TextField(controller: _amountIn, decoration: const InputDecoration(labelText: 'Amount In')),
          TextField(controller: _minOut, decoration: const InputDecoration(labelText: 'Min Out')),
          TextField(controller: _maxHops, decoration: const InputDecoration(labelText: 'Max Hops (1-4)')),
          TextField(controller: _deadlineSec, decoration: const InputDecoration(labelText: 'Deadline Seconds')),
          TextField(controller: _traderPub, decoration: const InputDecoration(labelText: 'Trader Public Key (hex)')),
          TextField(controller: _traderSig, decoration: const InputDecoration(labelText: 'Trader Signature (hex sans pq=)')),
          TextField(controller: _signPayload, decoration: const InputDecoration(labelText: 'Sign Payload (copy for signing)')),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            children: [
              FilledButton(onPressed: _refresh, child: const Text('Refresh Node')),
              FilledButton(onPressed: _quoteBestRoute, child: const Text('Quote Best Route')),
              FilledButton(onPressed: _getSignPayload, child: const Text('Get Sign Payload')),
              FilledButton(onPressed: _executeSignedBestRoute, child: const Text('Execute Signed Best Route')),
            ],
          ),
          const SizedBox(height: 16),
          _kpi('Best Amount Out', _bestAmountOut),
          const SizedBox(height: 8),
          _kpi('Best Route', _bestRoute),
          const SizedBox(height: 12),
          Text('Status: $_status'),
        ],
      ),
    );
  }

  Widget _kpi(String label, String value) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xFF181E2D),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: Colors.white70)),
          const SizedBox(height: 6),
          Text(value, style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }
}
