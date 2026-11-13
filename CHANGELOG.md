# Changelog

## 0.1.1

- Add English and Russian desktop language selection with system default.
- Improve mixer typography, fader visibility and light-theme contrast.
- Check both languages in the isolated GTK integration workflow.

## 0.1.0

- Keep the underlying MiniFuse control error when an ioctl fails, so an unavailable device is reported accurately.
- Explain when saved serial selection has not yet been applied to the running control module, including checks before reloading it.
- Record physical headphone/microphone/Ardour, USB port-change, host PipeWire restart and suspend/resume results with their limits in [hardware acceptance](docs/hardware-validation.md).
- Package tracked sources only, record source revision in `BUILD-INFO.json`, normalize archive output and verify every archived file and executable version.
- Prepare stable GitHub releases as reviewable drafts after CI; candidate tags remain prereleases. Upload review archives from branch builds too.
- Add a Russian first-run guide, Ubuntu runtime dependencies and a bounded 48 kHz/Auto support claim.

## 0.1.0-rc.1

- Compact studio desktop with a dark default and light alternative; the master stays visible while channels scroll.
- Solo, master Mute and Dim, precise numeric gain entry, and double-click fader/pan reset.
- Bounded Undo/Redo for software edits, keyboard shortcuts and mixer-draft recovery after reconnecting.
- Independent Linux names, Link and channel visibility, with portable `.json` presets and migration from earlier documents.
- Owned-file application updates with rollback, a single VERSION source and a checked draft GitHub prerelease workflow.
- DSP, installer and private-service regression checks plus an expanded 31-stage GTK integration check.

The tag was published with isolated software and initial physical coverage.
Further operator checks are recorded in [hardware acceptance](docs/hardware-validation.md);
the remaining acceptance work is tracked in [TODO.md](TODO.md).

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
