import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import 'kv_parser.dart';
import 'write_rpc_policy.dart';

typedef RpcTransport = Future<String> Function(String wire);

class WriteEndpoint {
  WriteEndpoint({
    required this.host,
    required this.port,
    this.token = '',
    this.scheme = 'text',
  });

  final String host;
  final int port;
  final String token;
  final String scheme;

  static WriteEndpoint parse(String raw, {String token = ''}) {
    var text = raw.trim();
    if (text.isEmpty) {
      text = '${WriteRpcPolicy.defaultHost}:${WriteRpcPolicy.defaultPort}';
    }
    var scheme = 'text';
    var host = WriteRpcPolicy.defaultHost;
    var port = WriteRpcPolicy.defaultPort;

    if (text.contains('://')) {
      final uri = Uri.parse(text);
      if (uri.scheme == 'http' || uri.scheme == 'https') {
        scheme = uri.scheme;
      } else if (uri.scheme == 'text') {
        scheme = 'text';
      } else {
        throw AdditionRpcException('unsupported RPC URL scheme');
      }
      host = uri.host.toLowerCase();
      if (uri.hasPort) {
        port = uri.port;
      } else if (scheme == 'https') {
        port = 443;
      } else if (scheme == 'http') {
        port = 80;
      }
    } else if (text.startsWith('[')) {
      final end = text.indexOf(']');
      if (end < 2) {
        throw AdditionRpcException('invalid IPv6 RPC host');
      }
      host = text.substring(1, end).toLowerCase();
      final rest = text.substring(end + 1);
      if (rest.startsWith(':')) {
        port = int.parse(rest.substring(1));
      }
    } else if (text.split(':').length == 2) {
      final parts = text.split(':');
      host = parts[0].toLowerCase();
      port = int.parse(parts[1]);
    } else {
      host = text.toLowerCase();
    }

    if (host.isEmpty) {
      throw AdditionRpcException('missing RPC host');
    }
    WriteRpcPolicy.assertWriteEndpoint(host);
    if (port <= 0 || port > 65535) {
      throw AdditionRpcException('invalid RPC port');
    }
    return WriteEndpoint(host: host, port: port, token: token, scheme: scheme);
  }

  String get display {
    if (scheme == 'http' || scheme == 'https') {
      return '$scheme://$host:$port';
    }
    return '$host:$port';
  }
}

class TextRpcClient {
  TextRpcClient({
    required this.endpoint,
    this.timeout = const Duration(seconds: 8),
    this.transport,
  }) {
    WriteRpcPolicy.assertWriteEndpoint(endpoint.host);
  }

  final WriteEndpoint endpoint;
  final Duration timeout;
  final RpcTransport? transport;
  final List<String> sent = [];

  Future<String> call(String command, {bool write = true}) async {
    WriteRpcPolicy.assertCommand(command, write: write);
    return _dispatch(command);
  }

  /// Operator console: any single-line TEXT RPC on loopback except insecure /
  /// foreign-chain commands. Still refuses non-loopback hosts.
  Future<String> consoleCall(String command) async {
    WriteRpcPolicy.assertWriteEndpoint(endpoint.host);
    if (command.contains('\n') || command.contains('\r')) {
      throw AdditionRpcException('RPC command must be a single line');
    }
    final token = firstCommandToken(command);
    if (token.isEmpty) {
      throw AdditionRpcException('empty RPC command');
    }
    final lowered = token.toLowerCase();
    if (WriteRpcPolicy.insecureCommands.contains(lowered)) {
      throw AdditionRpcException('refusing insecure RPC command: $token');
    }
    final parts = lowered.split('_').toSet();
    if (WriteRpcPolicy.foreignChainTokens.contains(lowered) ||
        parts.any(WriteRpcPolicy.foreignChainTokens.contains) ||
        WriteRpcPolicy.foreignChainTokens.any(lowered.contains)) {
      throw AdditionRpcException('ADDITION RPC only');
    }
    if (command.length > WriteRpcPolicy.maxRpcLine) {
      throw AdditionRpcException(
        'RPC command exceeds ${WriteRpcPolicy.maxRpcLine}-byte TEXT RPC limit',
      );
    }
    return _dispatch(command);
  }

  Future<String> _dispatch(String command) async {
    final wire =
        endpoint.token.isEmpty ? command : '${endpoint.token} $command';
    if (wire.length > WriteRpcPolicy.maxRpcLine) {
      throw AdditionRpcException(
        'RPC command exceeds ${WriteRpcPolicy.maxRpcLine}-byte TEXT RPC limit',
      );
    }
    sent.add(wire);

    String reply;
    try {
      if (transport != null) {
        reply = await transport!(wire);
      } else if (endpoint.scheme == 'http' || endpoint.scheme == 'https') {
        reply = await _httpGet(
          command: command,
          baseUrl: '${endpoint.scheme}://${endpoint.host}:${endpoint.port}/rpc',
          write: true,
        );
      } else {
        reply = await _socketSend(wire);
      }
    } on AdditionRpcException {
      rethrow;
    } on SocketException {
      throw AdditionRpcOfflineException();
    } on TimeoutException {
      throw AdditionRpcOfflineException();
    } on IOException {
      throw AdditionRpcOfflineException();
    } catch (_) {
      throw AdditionRpcOfflineException();
    }

    final text = reply.trim();
    if (text.isEmpty || text == 'RPC offline') {
      throw AdditionRpcOfflineException();
    }
    return text;
  }

  Future<String> _socketSend(String wire) async {
    final socket = await Socket.connect(
      endpoint.host,
      endpoint.port,
      timeout: timeout,
    );
    final completer = Completer<String>();
    final buffer = BytesBuilder(copy: false);
    Timer? timer;

    void finishOk() {
      timer?.cancel();
      if (completer.isCompleted) return;
      final raw = utf8.decode(buffer.takeBytes(), allowMalformed: true);
      completer.complete(raw.split('\n').first);
      socket.destroy();
    }

    void finishErr() {
      timer?.cancel();
      if (completer.isCompleted) return;
      completer.completeError(AdditionRpcOfflineException());
      socket.destroy();
    }

    timer = Timer(timeout, finishErr);
    socket.listen(
      (data) {
        buffer.add(data);
        if (data.contains(10) || buffer.length > WriteRpcPolicy.maxRpcLine) {
          finishOk();
        }
      },
      onDone: finishOk,
      onError: (_) => finishErr(),
      cancelOnError: true,
    );
    socket.add(utf8.encode('$wire\n'));
    await socket.flush();
    return completer.future;
  }

  Future<String> _httpGet({
    required String command,
    required String baseUrl,
    required bool write,
  }) async {
    WriteRpcPolicy.assertCommand(command, write: write);
    if (write) {
      WriteRpcPolicy.assertWriteEndpoint(baseUrl);
    } else if (!WriteRpcPolicy.isKnownPublicReadEndpoint(baseUrl)) {
      throw AdditionRpcException('unknown public read endpoint');
    }
    final uri = Uri.parse(baseUrl).replace(queryParameters: {'cmd': command});
    final res = await http.get(uri).timeout(timeout);
    final body = res.body.trim();
    if (body.isEmpty || body == 'RPC offline') {
      throw AdditionRpcOfflineException();
    }
    return body;
  }
}

/// Public-read getinfo only (status). Never used for createwallet/send.
Future<String> publicReadHttp(
  String command, {
  String url = WriteRpcPolicy.defaultPublicReadUrl,
  Duration timeout = const Duration(seconds: 8),
}) async {
  WriteRpcPolicy.assertCommand(command, write: false);
  if (!WriteRpcPolicy.isKnownPublicReadEndpoint(url)) {
    throw AdditionRpcException('unknown public read endpoint');
  }
  final uri = Uri.parse(url).replace(queryParameters: {'cmd': command});
  try {
    final res = await http.get(uri).timeout(timeout);
    final body = res.body.trim();
    if (body.isEmpty || body == 'RPC offline') {
      throw AdditionRpcOfflineException();
    }
    return body;
  } on AdditionRpcException {
    rethrow;
  } catch (_) {
    throw AdditionRpcOfflineException();
  }
}
