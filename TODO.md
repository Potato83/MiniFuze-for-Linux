# Release checklist and deferred work

## 0.1.0 release preparation

- [x] Set application version 0.1.0, align release notes and keep module version 0.2.0.
- [x] Exclude untracked/private files, validate binary versions and verify archive provenance/manifests.
- [x] Add deterministic source archives and checks for corruption, unsafe members, stale binaries and tag classification.
- [x] Document first launch, Ubuntu runtime dependencies, update/rollback and known support boundaries.
- [x] Prepare a stable-release draft workflow and branch-build review archives.
- [x] Pass 37 software tests and 31 isolated GTK stages before final packaging checks.
- [x] Compile the unchanged module for installed 6.18.60 LTS and 7.3.3 headers without loading it.
- [x] Update the real managed installation to 0.1.0 while preserving mix, selection and hardware switches.
- [x] Finish a 20-minute installed 0.1.0 playback/state/signal observation.
- [x] Verify preparation source/platform archives and extracted-source build/install checks.
- [x] Pass both Ubuntu CI jobs on the code preparation commit; repeat them for the release tag.
- [x] Verify the real boot into 7.3.4-1-cachyos: DKMS, serial-selected control, meters and 48 kHz running graph.
- [x] Build and verify native Ubuntu, Arch/CachyOS and optional DKMS packages; supplement and download-audit all 18 stable-draft assets.
- [x] Pass 47 software tests and Ubuntu GTK/archive/kernel CI after native packaging additions.
- [x] Final installed 0.1.0 check on 7.3.4: verified binaries, clean stereo music and 15-second microphone recording/replay, retained mix/defaults/clock.
- [x] Final draft review: all 18 existing assets and 9 SHA256 sidecars, original archive manifests and notes verified without replacing assets.
- [ ] Publish the stable draft when ready.

## Earlier candidate acceptance

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
- [x] Confirm mixer/control recovery after one suspend/resume cycle at 48 kHz.
- [x] Publish the checked prerelease: [v0.1.0-rc.1](https://github.com/Potato83/MiniFuze-for-Linux/releases/tag/v0.1.0-rc.1).
- [ ] Continue collecting user feedback after release.

## Distribution after 0.1.0

- [ ] Publish maintained AUR recipes and decide whether to host signed APT/pacman repositories.
- [ ] Exercise migration from manual DKMS on a physical host with backups.

## Next product improvements

- [ ] Manage application playback destinations from the panel, including reconnecting new streams.
- [ ] Named preset library with search and recent files (portable file presets already work).
- [ ] Diagnostic report with serial numbers and personal paths removed.
- [ ] RU/EN interface translation and broader accessibility/HiDPI acceptance.

## Remaining functional coverage

- [ ] MIDI DIN IN/OUT transfer using a real cable; enumeration is insufficient.
- [ ] Firmware image validation, update protocol and interrupted-update recovery.
- [ ] Complete Windows parity for unclassified preset fields and all modes.
- [ ] Prolonged C-service operation, repeated suspend cycles and DAW recovery
  beyond the short reconnect/restart/suspend checks already documented.
- [x] Boot an upgraded 7.3.4-1-cachyos kernel and check DKMS/control/48 kHz graph.
- [x] Headphone music and microphone recording/replay after the kernel upgrade at 48 kHz Auto.
- [ ] Fresh DAW acceptance after the kernel upgrade and other distributions/interfaces.
- [ ] Physical input/plugin delay compensation and broader DAW/rate combinations.
- [ ] Small-buffer 32/64 qualification, prolonged load and USB-hub fault cause.
- [ ] Electrical calibration of +48V, levels and hardware meters.
- [ ] Desktop translations, accessibility review and HiDPI coverage on other themes.

The normal stream-based mixer remains the default. Restricted experimental
latency variants from the research workspace are not enabled in this release.
