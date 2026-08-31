import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:addition_app/rpc/banners.dart';
import 'package:addition_app/rpc/write_rpc_policy.dart';
import 'package:addition_app/ui/core_shell.dart';
import 'package:addition_app/ui/theme.dart';

void main() {
  testWidgets('Addition Core boots with real nav and brand', (tester) async {
    tester.view.physicalSize = const Size(1280, 800);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    await tester.pumpWidget(
      MaterialApp(
        theme: AdditionTheme.dark(),
        home: const CoreShell(autoConnect: false, enableVideo: false),
      ),
    );

    expect(find.text('ADDITION'), findsWidgets);
    expect(find.text('Core'), findsOneWidget);
    expect(find.text(WriteRpcPolicy.productNetworkId), findsOneWidget);

    // Real nav destinations (not pipe links).
    expect(find.text('Wallet'), findsWidgets);
    expect(find.text('Receive'), findsWidgets);
    expect(find.text('Send'), findsWidgets);
    expect(find.text('Mine'), findsWidgets);
    expect(find.text('Peers'), findsWidgets);
    expect(find.text('Console'), findsWidgets);

    // No forbidden product surfaces.
    expect(find.textContaining('SmartChain'), findsNothing);
    expect(find.textContaining('Best Route'), findsNothing);
    expect(find.textContaining('Swap'), findsNothing);
    expect(find.textContaining('Solidity'), findsNothing);

    // Banner URLs are the durable live ones (wired in constants / posters).
    expect(
      AdditionBanners.banner1Url,
      contains('additionblockchain.com/banners/addition-banner-1.mp4'),
    );
    expect(
      AdditionBanners.banner2Url,
      contains('additionblockchain.com/banners/addition-banner-2.mp4'),
    );

    // Navigate to each page.
    await tester.tap(find.text('Receive').first);
    await tester.pump();
    expect(find.textContaining('No wallet loaded'), findsOneWidget);

    await tester.tap(find.text('Send').first);
    await tester.pump();
    expect(find.textContaining('loopback'), findsWidgets);

    await tester.tap(find.text('Mine').first);
    await tester.pump();
    expect(find.textContaining('finding miner'), findsOneWidget);

    await tester.tap(find.text('Peers').first);
    await tester.pump();
    expect(find.textContaining('No invented'), findsOneWidget);

    await tester.tap(find.text('Console').first);
    await tester.pump();
    expect(find.textContaining('TEXT RPC'), findsWidgets);
  });
}
