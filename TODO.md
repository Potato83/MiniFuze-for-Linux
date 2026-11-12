# Release checklist and deferred work

## 0.1.0-rc.1 release acceptance

- [x] Compact dark studio UI with a light alternative.
- [x] Solo, master Mute/Dim, numeric gain, Undo/Redo and Linux preset files.
- [x] Names, Link and visibility without importing a Windows template.
- [x] Local DSP, update/rollback, private service recovery and GTK checks.
- [x] Versioned source/binary archives and checked prerelease workflow.
- [x] Confirm GitHub Actions on Ubuntu 24.04, including kernel compilation.
- [x] Measure the current C candidate through OUTPUT L → INPUT 2 at 48 kHz.
- [x] Exercise software controls/presets and hardware switch readback on MiniFuse 2.
- [x] Confirm same-port USB unplug/replug, owned-link repair and engine restart.
- [x] Verify a fresh installation and active user-service update on CachyOS.
- [x] Listen to stereo music through MiniFuse headphones at 48 kHz (short operator check).
- [x] Confirm Main Mute, Dim and the Main fader by listening to music.
- [x] Record and replay an XLR microphone with +48V; confirm software monitoring through headphones.
- [x] Confirm that Direct Mono routes Input 1 to both headphones with MIX toward INPUT.
- [x] Confirm that direct stereo monitoring routes Input 1 to the left headphone.
- [x] Exercise Ardour 9.8 music playback, simultaneous microphone recording, saving and voice replay at 48 kHz.
- [x] Confirm USB port changes retain audio/mix and, after applying serial selection, hardware controls/meters.
- [x] Confirm mixer recovery after full host PipeWire restart; a JACK DAW may require audio-engine reconnection.
- [ ] Confirm suspend/resume on hardware.
- [x] Publish the checked prerelease: [v0.1.0-rc.1](https://github.com/Potato83/MiniFuze-for-Linux/releases/tag/v0.1.0-rc.1).
- [ ] Collect feedback before declaring a stable release.

## Next product improvements

- [ ] Manage application playback destinations from the panel, including reconnecting new streams.
- [ ] Named preset library with search and recent files (portable file presets already work).
- [ ] Diagnostic report with serial numbers and personal paths removed.
- [ ] RU/EN interface translation and broader accessibility/HiDPI acceptance.

## Remaining functional coverage

- [ ] MIDI DIN IN/OUT transfer using a real cable; enumeration is insufficient.
- [ ] Firmware image validation, update protocol and interrupted-update recovery.
- [ ] Complete Windows parity for unclassified preset fields and all modes.
- [ ] C-service hotplug, suspend and long-session qualification beyond existing
  tests of the earlier implementation.
- [ ] Real kernel-upgrade/DKMS test and other distributions/interfaces.
- [ ] Physical input/plugin delay compensation and broader DAW/rate combinations.
- [ ] Small-buffer 32/64 qualification, prolonged load and USB-hub fault cause.
- [ ] Electrical calibration of +48V, levels and hardware meters.
- [ ] Desktop translations, accessibility review and HiDPI coverage on other themes.

The normal stream-based mixer remains the default. Restricted experimental
latency variants from the research workspace are not enabled in this release.
