import 'package:addition_app/rpc/kv_parser.dart';
import 'package:addition_app/rpc/text_rpc_client.dart';
import 'package:addition_app/rpc/wallet_client.dart';
import 'package:addition_app/rpc/wallet_commands.dart';
import 'package:addition_app/rpc/write_rpc_policy.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('WriteRpcPolicy', () {
    test('allows loopback write hosts', () {
      expect(WriteRpcPolicy.assertWriteEndpoint('127.0.0.1'), '127.0.0.1');
      expect(WriteRpcPolicy.assertWriteEndpoint('localhost:8545'), 'localhost');
      expect(WriteRpcPolicy.assertWriteEndpoint('::1'), '::1');
    });

    test('refuses public write hosts', () {
      expect(
        () => WriteRpcPolicy.assertWriteEndpoint('rpc.additionblockchain.com'),
        throwsA(isA<AdditionRpcException>()),
      );
      expect(
        () => WriteRpcPolicy.assertWriteEndpoint('34.27.30.115:8545'),
        throwsA(isA<AdditionRpcException>()),
      );
      expect(
        () => WriteRpcPolicy.assertWriteEndpoint('8.8.8.8'),
        throwsA(isA<AdditionRpcException>()),
      );
      expect(
        () => WriteRpcPolicy.assertWriteEndpoint('192.168.1.10'),
        throwsA(isA<AdditionRpcException>()),
      );
    });

    test('honest network labels', () {
      expect(
        WriteRpcPolicy.networkLabel({'network': 'testnet'}),
        'testnet',
      );
      final main = WriteRpcPolicy.networkLabel({'network': 'mainnet'});
      expect(main.contains('mainnet'), isTrue);
      expect(main.toLowerCase().contains('live public'), isTrue);
      expect(main.toLowerCase().contains('not a live'), isTrue);
    });

    test('parse getinfo and balance', () {
      final info = WriteRpcPolicy.parseGetinfo(
        'network=testnet height=12 peers=1 pq_mode=strict',
      );
      expect(info['network'], 'testnet');
      expect(info['height'], '12');
      expect(
        WriteRpcPolicy.parseConfirmedBalance('confirmed=50 pending=0'),
        50,
      );
    });
  });

  group('wallet commands', () {
    test('build createwallet / send', () {
      expect(buildCreatewallet('default'), 'createwallet default');
      expect(
        buildWalletSend('alice', 'a' * 128, 10, fee: 1),
        'wallet_send alice ${'a' * 128} 10 1',
      );
    });

    test('rejects decimals and bad names', () {
      expect(() => parseWholeAmount('1.5'), throwsA(isA<AdditionRpcException>()));
      expect(() => validateWalletName(''), throwsA(isA<AdditionRpcException>()));
      expect(
        () => validateAddress('0xabc'),
        throwsA(isA<AdditionRpcException>()),
      );
    });

    test('refuses insecure commands', () {
      expect(
        () => WriteRpcPolicy.assertCommand('sendtx abc', write: true),
        throwsA(isA<AdditionRpcException>()),
      );
      expect(
        () => WriteRpcPolicy.assertCommand('wallet_send x', write: false),
        throwsA(isA<AdditionRpcException>()),
      );
    });
  });

  group('WalletRpcClient with mock transport', () {
    test('createwallet accepts priv_printed=0 and refuses real priv fields', () async {
      final endpoint = WriteEndpoint.parse('127.0.0.1:8545');
      final ok = TextRpcClient(
        endpoint: endpoint,
        transport: (wire) async {
          expect(wire.startsWith('createwallet'), isTrue);
          return 'name=default address=${'ab' * 64} algo=ml-dsa-87 priv_printed=0';
        },
      );
      final record = await WalletRpcClient(ok).createwallet();
      expect(record.address.length, 128);
      expect(record.algorithm, 'ml-dsa-87');

      final leak = TextRpcClient(
        endpoint: endpoint,
        transport: (wire) async =>
            'name=default address=${'ab' * 64} private_key=deadbeef',
      );
      await expectLater(
        WalletRpcClient(leak).createwallet(),
        throwsA(isA<AdditionRpcException>()),
      );
    });

    test('send uses wallet_send and checks confirmation', () async {
      final endpoint = WriteEndpoint.parse('127.0.0.1:8545');
      final calls = <String>[];
      final rpc = TextRpcClient(
        endpoint: endpoint,
        transport: (wire) async {
          calls.add(wire);
          if (wire.startsWith('wallet_info')) {
            return 'name=alice address=${'cd' * 64} algo=ml-dsa-87';
          }
          if (wire.startsWith('wallet_send')) {
            return 'ok:gossiped hash=deadbeef amount=5 to=${'ee' * 64}';
          }
          throw AdditionRpcException('unexpected: $wire');
        },
      );
      final client = WalletRpcClient(rpc);
      final reply = await client.send(
        name: 'alice',
        to: 'ee' * 64,
        amount: 5,
        fee: 0,
      );
      expect(reply.contains('ok:gossiped'), isTrue);
      expect(calls.any((c) => c.startsWith('wallet_send')), isTrue);
      expect(calls.any((c) => c.startsWith('sendtx')), isFalse);
    });

    test('WriteEndpoint.parse refuses public hosts', () {
      expect(
        () => WriteEndpoint.parse('rpc.additionblockchain.com:8545'),
        throwsA(isA<AdditionRpcException>()),
      );
    });
  });

  group('kv parser', () {
    test('parses tokens', () {
      final m = parseKv('network=testnet height=3');
      expect(m['network'], 'testnet');
      expect(firstCommandToken('wallet_send a b'), 'wallet_send');
    });
  });
}
