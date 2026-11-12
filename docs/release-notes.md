# MiniFuse 2 for Linux 0.1.0-rc.1

This release candidate adds a compact studio mixer and independent Linux presets.

- Graphite desktop by default, with a light studio theme in Settings. Existing theme preferences are retained.
- Master channel stays visible while source channels scroll. Hardware switches are in the bottom bar; device selection and loopback settings are in the Device menu.
- Solo per source, master Mute and Dim (−20 dB), precise numeric gain entry, and double-click reset for faders and pan.
- Undo/Redo for the last 32 software changes, Ctrl+Z / Ctrl+Shift+Z, Ctrl+S and Ctrl+O.
- Names, input Link and virtual-channel visibility work immediately without a Windows preset.
- Open/save portable Linux `.json` presets, including all three mixes and presentation. Versions 1–3 of the earlier state files remain readable.
- A mixer draft survives reconnection to the service. Hardware commands are not replayed after a connection failure.
- `bash tools/install_desktop.sh --update` updates owned files, keeps settings and restores previous files if updating fails.

Build from the source archive with the dependencies in README.md, or use the
Ubuntu 24.04 x86_64 binary archive on that system. Its binaries link to Ubuntu's
system libraries; it is not a universal Linux package. Run
`bash tools/install_desktop.sh` without sudo. For an existing managed installation,
close the window and use `--update`; an active owned mixer service briefly stops
and restarts. Reopen the desktop afterwards.

Solo, Dim and master Mute apply to the selected software mix. Mute takes priority
over Solo. They are preserved in Linux presets. Turn them off before Windows
`.mfp` export; export needs an imported Windows template to retain its unknown fields.
Presets never switch +48V/INST or change the global sample rate and buffer.

This candidate has local automated software and isolated GTK coverage, plus a
48 kHz OUTPUT L → INPUT 2 measurement on MiniFuse 2 firmware 1.5.0. Gain, pan,
Solo, Dim, Mute, presets, engine restart, owned-link repair and same-port USB
reconnect passed. Hardware switch readback and an actual user-service update
also passed; +48V was left off during those automated checks. Subsequent operator
checks covered headphone music playback, Main Mute/Dim/fader, XLR microphone
capture with +48V, direct mono/stereo and software monitoring. A short Ardour 9.8
session at 48 kHz also passed music playback, simultaneous microphone recording,
session saving and recorded-voice playback; Ardour reported no xruns in that session.
Suspend recovery, port changes and long-session qualification remain pending.
MIDI DIN transfer, firmware updates, small-buffer qualification and
additional distribution/hardware combinations remain outside the supported claims.
The separate kernel module remains version 0.2.0.
Detailed scope and numeric reports are in `docs/hardware-validation.md`.

Verify archive hashes using the accompanying `.sha256` file. Source archives also
include an internal SHA256SUMS manifest. The release workflow publishes a
prerelease after both Ubuntu application tests and kernel compilation pass.
