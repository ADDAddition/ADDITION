import 'package:flutter/material.dart';
import 'package:video_player/video_player.dart';

import '../rpc/banners.dart';
import '../rpc/write_rpc_policy.dart';
import 'theme.dart';

/// Sober About / splash for Addition Core.
///
/// Shows **one** muted looping clip from the durable live banner URLs
/// (not dual autoplay tiles, not Jeremy's website logo stinger).
class AboutAdditionCoreDialog extends StatefulWidget {
  const AboutAdditionCoreDialog({super.key, this.enableVideo = true});

  final bool enableVideo;

  static Future<void> show(BuildContext context, {bool enableVideo = true}) {
    return showDialog<void>(
      context: context,
      builder: (ctx) => AboutAdditionCoreDialog(enableVideo: enableVideo),
    );
  }

  @override
  State<AboutAdditionCoreDialog> createState() =>
      _AboutAdditionCoreDialogState();
}

class _AboutAdditionCoreDialogState extends State<AboutAdditionCoreDialog> {
  VideoPlayerController? _controller;
  bool _ready = false;

  @override
  void initState() {
    super.initState();
    if (widget.enableVideo) {
      _start();
    }
  }

  Future<void> _start() async {
    // Single sober clip for About — banner-1 only (not dual tiles, not stinger).
    final controller = VideoPlayerController.networkUrl(
      Uri.parse(AdditionBanners.banner1Url),
    );
    _controller = controller;
    try {
      await controller.initialize();
      await controller.setLooping(true);
      await controller.setVolume(0);
      await controller.play();
      if (mounted) setState(() => _ready = true);
    } catch (_) {
      await controller.dispose();
      _controller = null;
      if (mounted) setState(() => _ready = false);
    }
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: AdditionTheme.panel,
      title: const Text(
        'About Addition Core',
        style: TextStyle(color: AdditionTheme.cream),
      ),
      content: SizedBox(
        width: 420,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            ClipRRect(
              borderRadius: BorderRadius.circular(6),
              child: AspectRatio(
                aspectRatio: 16 / 9,
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    Image.asset(
                      AdditionBanners.banner1PosterAsset,
                      fit: BoxFit.cover,
                      errorBuilder: (context, error, stackTrace) =>
                          const ColoredBox(color: AdditionTheme.ink),
                    ),
                    if (_ready && _controller != null)
                      FittedBox(
                        fit: BoxFit.cover,
                        clipBehavior: Clip.hardEdge,
                        child: SizedBox(
                          width: _controller!.value.size.width,
                          height: _controller!.value.size.height,
                          child: VideoPlayer(_controller!),
                        ),
                      ),
                    const ColoredBox(color: Color(0x66000000)),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 14),
            const Text(
              'ADDITION',
              style: TextStyle(
                color: AdditionTheme.logoRed,
                fontWeight: FontWeight.w800,
                fontSize: 20,
                letterSpacing: 1.2,
              ),
            ),
            const SizedBox(height: 4),
            const Text(
              'Addition Core — full-node desktop GUI',
              style: TextStyle(color: AdditionTheme.cream, fontSize: 14),
            ),
            const SizedBox(height: 8),
            Text(
              WriteRpcPolicy.productNetworkId,
              style: const TextStyle(
                color: AdditionTheme.mute,
                fontFamily: 'monospace',
                fontSize: 12,
              ),
            ),
            const SizedBox(height: 10),
            const Text(
              'Wallet, receive, send, mine, peers, and console against a local '
              'additiond. Write RPC is loopback-only. Not a DEX, not SmartChain, '
              'not a hosted custodial wallet.',
              style: TextStyle(
                color: AdditionTheme.mute,
                fontSize: 13,
                height: 1.35,
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Close'),
        ),
      ],
    );
  }
}
