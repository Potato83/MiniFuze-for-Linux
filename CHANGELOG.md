# Changelog

## 0.1.0-preview

Initial public preview, prepared for publication.

- Native C/GTK 4 desktop with light and dark themes and a separate C mixer service.
- Three stereo software mixes, virtual playback/recording endpoints, gain,
  pan, mute, linked strips, names and visibility settings.
- Linux presets, working-state recovery and Windows .mfp import/export.
- Hardware switches and input meters through the bounded 0.2.0 kernel module.
- Shared PipeWire rate/buffer controls with actual-state checks and Auto fallback.
- English and Russian research reports, installation instructions and test fixtures.

Physical testing covers one MiniFuse 2 with firmware 1.5.0 on CachyOS x86_64.
See [TODO.md](TODO.md) for unimplemented and unqualified behavior.
