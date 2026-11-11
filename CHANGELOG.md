# Changelog

## 0.1.0-rc.1

- Compact studio desktop with a dark default and light alternative; the master stays visible while channels scroll.
- Solo, master Mute and Dim, precise numeric gain entry, and double-click fader/pan reset.
- Bounded Undo/Redo for software edits, keyboard shortcuts and mixer-draft recovery after reconnecting.
- Independent Linux names, Link and channel visibility, with portable `.json` presets and migration from earlier documents.
- Owned-file application updates with rollback, a single VERSION source and a checked draft GitHub prerelease workflow.
- DSP, installer and private-service regression checks plus an expanded 31-stage GTK integration check.

New studio behavior has isolated software coverage. Physical acceptance of this
candidate remains pending; the hardware support limits below still apply.

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
