# MiniFuse 2 for Linux 0.1.0

An independent Linux mixer and control panel for **Arturia MiniFuse 2**. This
release brings the compact GTK desktop, software mixing and tested hardware
controls together in an installable application.

- Compact dark mixer with an optional light theme and a master strip that stays visible.
- Three stereo mixes, source gain/pan/Mute/Solo, master Mute and Dim (−20 dB).
- Numeric level entry, double-click reset and 32-step Undo/Redo.
- Portable Linux presets, working-mix recovery, names, input Link and channel visibility.
- Four stereo playback endpoints and four recording endpoints in PipeWire.
- INST 1/2, shared +48V, Direct Mono, hardware loopback controls and input meters through the separate control module.
- Owned-file installation and update with rollback. Closing the window keeps the audio service running.
- Clearer control-device errors and installation guidance for selecting a card across USB ports.

## Downloads and installation

Use the **Ubuntu 24.04 x86_64** archive on Ubuntu 24.04, or build the source
archive on another supported Linux setup. The binary archive uses system
libraries; it is not a universal Linux package. Both archives include source,
installation tools, licenses and a `BUILD-INFO.json` recording version and
source commit. SHA256 sidecars and an internal file manifest accompany the files.

Verify the downloaded archive before extracting it:

```sh
sha256sum -c minifuze2-0.1.0-ubuntu-24.04-x86_64.tar.gz.sha256
tar -xzf minifuze2-0.1.0-ubuntu-24.04-x86_64.tar.gz
cd minifuze2-0.1.0
sha256sum -c SHA256SUMS
bash tools/install_desktop.sh
```

Run the application installer as your desktop user. For an existing managed
installation, close the window and use `bash tools/install_desktop.sh --update`.
The mixer briefly restarts; presets and service settings are retained. Extra
hardware controls need the separate **0.2.0** kernel module and matching kernel
headers. See `docs/installation.md` and the Russian `docs/quickstart.ru.md`.

## Tested scope

Physical qualification uses one MiniFuse 2 with firmware **1.5.0**, CachyOS
x86_64 and kernel **7.3.1-1-cachyos**. The qualified audio mode is **48 kHz
with automatic rate/buffer settings**. Ubuntu 24.04 receives CI build, software,
GTK, archive and kernel compilation checks; physical Ubuntu qualification is
not available.

Hardware evidence covers analog loopback, headphone playback, Main Mute/Dim,
XLR microphone recording with +48V, direct mono/stereo and software monitoring,
and an Ardour 9.8 playback/recording session. USB reconnect and port changes,
installed service updates, host PipeWire restart and one suspend/resume cycle
also recovered the mixer. The control module must already use serial selection
for hardware controls to follow a USB-port change. Detailed results and their
limits are in `docs/hardware-validation.md`.

## Known limits

- Disconnecting USB or restarting PipeWire interrupts audio. Ardour's JACK engine needed reconnection after the host PipeWire restart; its saved project was retained.
- Firmware updates and physical MIDI DIN transfer are not supported by this application.
- Other sample rates, small buffers, repeated suspend, booting an updated kernel and other distributions/devices still need physical qualification. Compiling a module for another kernel does not qualify operation on it.
- The desktop currently uses Russian. English translation, accessibility and broader HiDPI testing remain future work.
- Linux presets retain Solo/Dim/master Mute; Windows `.mfp` export requires disabling those controls and an imported Windows template. Back up presets before returning to an older application: this release writes Linux document version 4.
- Presets do not switch +48V/INST, select a device or change shared PipeWire rate/buffer settings. Manual clock changes affect the host and are refused while another physical audio device is active.

This project is independent of Arturia. The kernel control module is optional
for standard ALSA audio and does not implement firmware writing.
