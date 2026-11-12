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

Native application packages are available for **Ubuntu 24.04 amd64**, **Arch Linux
x86_64** and **CachyOS x86_64**. The optional **minifuze2-ctl-dkms 0.2.0** package
is separate. Each download has a SHA256 sidecar. Download the package for your
system and install it with `sudo apt install ./FILE.deb` or
`sudo pacman -U ./FILE.pkg.tar.zst`; dependencies come from your distribution.
Arch and CachyOS binaries are built separately because their library ABIs may
differ. AUR-ready source bundles are included; AUR submission and a project
update repository remain future work.

See [package installation and migration](https://github.com/Potato83/MiniFuze-for-Linux/blob/main/docs/packages.md)
before replacing a script/manual installation. Packages do not restart a running
mixer, reload the control module or select a device during installation. After a
fresh DKMS install, use `sudo minifuze2-ctl-setup --user YOUR_DESKTOP_ACCOUNT`
to select one connected MiniFuse and grant access. Matching kernel headers are
required. Existing differing settings are retained for review.

The native packages use the original application source commit **676bec2**;
package metadata also records the later packaging revision. The original
source/binary archives and `v0.1.0` tag are retained.

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

The installed 0.1.0 also completed a 20-minute playback observation: 238 state
checks and 40 complete one-second digital buffers with no clipping. The operator
confirmed clean music. The test sampled audio periodically and restarted the
file between passes; it does not claim gapless playback or zero xruns.

The final installed-version check on **7.3.4-1-cachyos** verified both 0.1.0
executables against their installation manifest. A complete one-second digital
monitor buffer contained stereo music without clipping. The 15-second microphone
file contained exactly 720,000 stereo 24-bit frames at 48 kHz, with no clipped
samples. Input 1 peaked at −0.19 dBFS; slightly lower hardware gain is recommended
for more headroom. The operator confirmed clean music and recorded voice in both
headphones. Mix, selected card, engine process, default devices and clock settings
were retained. The operator enabled +48V before recording, and that setting was
retained. No voice recording or personal data is included in this release. See the
[final audio report](https://github.com/Potato83/MiniFuze-for-Linux/blob/main/docs/validation/pre-release-0.1.0.json).

## Known limits

- Disconnecting USB or restarting PipeWire interrupts audio. Ardour's JACK engine needed reconnection after the host PipeWire restart; its saved project was retained.
- Firmware updates and physical MIDI DIN transfer are not supported by this application.
- Other sample rates, small buffers, repeated suspend and other distributions/devices still need physical qualification. After a real upgrade to kernel 7.3.4-1-cachyos, DKMS, serial-selected control and hardware meters were checked. Final headphone music playback and a 15-second XLR microphone recording/replay also passed at 48 kHz Auto; the operator confirmed clean stereo music and voice in both headphones. A new DAW session on that kernel remains untested. Compiling a module for another kernel does not qualify operation on it.
- The desktop currently uses Russian. English translation, accessibility and broader HiDPI testing remain future work.
- Linux presets retain Solo/Dim/master Mute; Windows `.mfp` export requires disabling those controls and an imported Windows template. Back up presets before returning to an older application: this release writes Linux document version 4.
- Presets do not switch +48V/INST, select a device or change shared PipeWire rate/buffer settings. Manual clock changes affect the host and are refused while another physical audio device is active.

This project is independent of Arturia. The kernel control module is optional
for standard ALSA audio and does not implement firmware writing.
