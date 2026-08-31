import 'dart:async';

import 'package:flutter/material.dart';
import 'package:video_player/video_player.dart';

import '../rpc/banners.dart';
import 'theme.dart';

/// Muted looping banner used as desktop window chrome background.
///
/// Plays one durable live MP4 at a time (rotating the two official URLs).
/// Not dual autoplay tiles — Bitcoin Core-style atmosphere behind the UI.
class CoreBannerBackground extends StatefulWidget {
  const CoreBannerBackground({
    super.key,
    this.enableVideo = true,
    this.child,
  });

  final bool enableVideo;
  final Widget? child;

  @override
  State<CoreBannerBackground> createState() => _CoreBannerBackgroundState();
}

class _CoreBannerBackgroundState extends State<CoreBannerBackground> {
  static const _rotateEvery = Duration(seconds: 28);

  int _index = 0;
  VideoPlayerController? _controller;
  bool _ready = false;
  Timer? _rotate;

  String get _poster => AdditionBanners.posters[_index];

  @override
  void initState() {
    super.initState();
    if (widget.enableVideo) {
      _start(_index);
      _rotate = Timer.periodic(_rotateEvery, (_) => _advance());
    }
  }

  Future<void> _advance() async {
    if (!mounted || !widget.enableVideo) return;
    final next = (_index + 1) % AdditionBanners.urls.length;
    await _start(next);
  }

  Future<void> _start(int index) async {
    final previous = _controller;
    final controller = VideoPlayerController.networkUrl(
      Uri.parse(AdditionBanners.urls[index]),
    );
    try {
      await controller.initialize();
      await controller.setLooping(true);
      await controller.setVolume(0); // muted
      await controller.play();
      if (!mounted) {
        await controller.dispose();
        return;
      }
      setState(() {
        _index = index;
        _controller = controller;
        _ready = true;
      });
      await previous?.dispose();
    } catch (_) {
      await controller.dispose();
      if (mounted) {
        setState(() {
          _index = index;
          _controller = null;
          _ready = false;
        });
      }
      await previous?.dispose();
    }
  }

  @override
  void dispose() {
    _rotate?.cancel();
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      fit: StackFit.expand,
      children: [
        ColoredBox(color: AdditionTheme.ink),
        Image.asset(
          _poster,
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
        // Dark scrim so operator chrome stays readable (Bitcoin Core-like).
        const ColoredBox(color: Color(0xCC0A0A0C)),
        if (widget.child != null) widget.child!,
      ],
    );
  }
}

/// Thin brand band under the title bar — single muted video, not two tiles.
class CoreBannerBand extends StatelessWidget {
  const CoreBannerBand({super.key, this.enableVideo = true, this.height = 72});

  final bool enableVideo;
  final double height;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: height,
      width: double.infinity,
      child: ClipRect(
        child: CoreBannerBackground(enableVideo: enableVideo),
      ),
    );
  }
}
