# 0.1.0 release preparation evidence

Date: 2026-11-12. Physical host: CachyOS x86_64, kernel 7.3.1-1-cachyos,
PipeWire 1.6.11, one MiniFuse 2 with firmware 1.5.0. The application is 0.1.0;
the separate control module remains 0.2.0. Qualified audio mode: 48 kHz with
rate and buffer set to Auto.

| Check | Result | Evidence and limits |
| --- | --- | --- |
| Local application build | Pass | Both C executables report 0.1.0; GTK 4.24.1 and libsoup 3.8.1 on this host |
| Local software suite | Pass | 37 tests, including real isolated runtime, installer rollback and eight archive/tag checks; no skipped tests |
| Isolated GTK | Pass | 31 stages, including themes, small window, controls, presets and reconnect draft; no physical routes |
| Source archive round-trip | Pass | Verified, extracted, built and ran all 37 tests from the 7a869a3 preparation archive |
| Binary archive installation | Pass | Ten isolated install/update/removal/rollback tests using stripped binaries extracted from the CachyOS archive |
| Real installed update | Pass | [Report](installed-update-0.1.0.json); mixer, device selection and hardware switches retained |
| Additional kernel compilation | Pass | Unchanged module compiled against installed 6.18.60-2-cachyos-lts and 7.3.3-1-cachyos headers in private source copies; neither module was loaded |
| Ubuntu 24.04 CI | Pass | Both [application/archive and kernel jobs](https://github.com/Potato83/MiniFuze-for-Linux/actions/runs/43222715052) passed for preparation commit 7a869a3; review archives uploaded |
| Installed endurance observation | Pass | [20-minute report](endurance-0.1.0.json): 238 state checks, 40 complete one-second digital buffers, no clipping; operator confirmed clean music |

The 0.1.0 preparation changes the version, control-error reporting and release
tooling/documentation. DSP and kernel behavior remain those exercised by the
earlier candidate's [physical reports](../hardware-validation.md). USB-port,
PipeWire-restart and suspend reports describe that earlier installed candidate;
they are not relabelled as fresh 0.1.0 tests.

Archive checks exclude untracked files, reject changed tracked sources and
symlinks, validate both binary versions, compare complete manifests and detect
corruption/unsafe archive members. Source archives repackage identically with
the same tooling, including from a verified extraction without Git. Every final
archive records its own source revision in `BUILD-INFO.json`; old candidate
archives are preserved.

The endurance test kept the same engine process, mix, device, hardware switches
and system defaults. Of 40 complete stereo buffers, 39 contained signal and one
was silent near the third file boundary. The supplied file ends with silence and
playback restarted between passes; exact source/capture clock alignment was not
measured. This is periodic observation, not a continuous analog recording or a
claim of zero xruns. Test playback was stopped when the observation ended.

A later real boot into **7.3.4-1-cachyos** passed a bounded read-only
[post-upgrade check](kernel-upgrade-0.1.0.json): DKMS 0.2.0 installed for the
running kernel, matching vermagic, serial-selected control endpoint, matching
control/audio ports, active hardware meters and a running 48 kHz playback/capture
graph with rate/quantum Auto. No fresh listening or DAW test was performed on
that kernel, and no native-package migration was performed on the physical host.

Release scope excludes firmware writing, physical MIDI DIN qualification,
small buffers and other rates, electrical calibration and repeated suspend.
Compiling a module alone does not qualify operation after a kernel upgrade.
The earlier host PipeWire restart needed a separate Ardour audio-engine reconnection.

Native Ubuntu/Arch/CachyOS packages and the draft asset audit are recorded
separately in the [native package acceptance](native-packages-0.1.0.md).
