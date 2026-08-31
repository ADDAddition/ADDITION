import 'dart:convert';
import 'dart:io';

import 'write_rpc_policy.dart';

/// Loopback-only client for the local EVM JSON-RPC bridge
/// (`web/evm/evm_rpc_bridge.py` on 127.0.0.1:9545).
///
/// Honest: not Ethereum mainnet, send disabled, not live Uniswap/ETH/XMR.
class EvmJsonRpcClient {
  EvmJsonRpcClient({
    this.host = '127.0.0.1',
    this.port = 9545,
    HttpClient? httpClient,
  }) : _http = httpClient ?? HttpClient() {
    WriteRpcPolicy.assertWriteEndpoint('$host:$port');
  }

  final String host;
  final int port;
  final HttpClient _http;

  static const defaultUrl = 'http://127.0.0.1:9545';
  static const expectedChainIdHex = '0x67932'; // 424242

  Uri get uri => Uri.parse('http://$host:$port/');

  Future<dynamic> call(String method, [List<dynamic> params = const []]) async {
    WriteRpcPolicy.assertWriteEndpoint('$host:$port');
    final body = jsonEncode({
      'jsonrpc': '2.0',
      'id': 1,
      'method': method,
      'params': params,
    });
    final req = await _http.postUrl(uri).timeout(const Duration(seconds: 4));
    req.headers.set(HttpHeaders.contentTypeHeader, 'application/json');
    req.add(utf8.encode(body));
    final resp = await req.close().timeout(const Duration(seconds: 4));
    final text = await resp.transform(utf8.decoder).join();
    if (resp.statusCode < 200 || resp.statusCode >= 300) {
      throw EvmRpcException('HTTP ${resp.statusCode}: $text');
    }
    final decoded = jsonDecode(text);
    if (decoded is! Map) {
      throw EvmRpcException('invalid JSON-RPC response');
    }
    if (decoded['error'] != null) {
      final err = decoded['error'];
      throw EvmRpcException(err is Map ? '${err['message']}' : '$err');
    }
    return decoded['result'];
  }

  Future<String> ethChainId() async {
    final r = await call('eth_chainId');
    return r?.toString() ?? '';
  }

  Future<Map<String, dynamic>> additionNetworkInfo() async {
    final r = await call('addition_networkInfo');
    if (r is Map) {
      return Map<String, dynamic>.from(r);
    }
    throw EvmRpcException('addition_networkInfo returned non-object');
  }

  Future<String> additionDisclaimer() async {
    final r = await call('addition_disclaimer');
    return r?.toString() ?? '';
  }

  void close() => _http.close(force: true);
}

class EvmRpcException implements Exception {
  EvmRpcException(this.message);
  final String message;
  @override
  String toString() => message;
}
