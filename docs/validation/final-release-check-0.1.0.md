# Final release check: 0.1.0

Date: 2026-11-12. Result: **pass within the documented 48 kHz Auto scope**.
The release remains a draft; this check does not publish it.

| Check | Result | Evidence |
| --- | --- | --- |
| Installed application | Pass | Both 0.1.0 binaries matched the owned-file installation manifest; running engine matched the installed executable |
| Updated physical kernel | Pass | 7.3.4-1-cachyos, module 0.2.0, matching vermagic, DKMS installed, control endpoint and hardware meters available |
| Headphone music | Pass | Operator confirmed clean stereo; a full one-second digital buffer had signal in both channels and no clipping |
| XLR microphone recording/replay | Pass | 15 seconds, 720,000 stereo frames, 24 bits at 48 kHz; no clipped samples. Input 1 extracted without PCM changes; operator confirmed clean voice in both headphones |
| State preservation | Pass | Same engine process, mix, preset, device, default metadata and clock metadata; rate/quantum Auto, playback/capture 48 kHz |
| Software and GTK | Pass | 49 software tests and 31 isolated GTK stages in the [final CI run](https://github.com/Potato83/MiniFuze-for-Linux/actions/runs/43247488651) |
| Kernel compilation in CI | Pass | Same final workflow, kernel job; no physical module reload |
| Release download audit | Pass | All 18 existing assets downloaded twice; nine SHA256 sidecars and original archive manifests checked. Asset identities and bytes unchanged; notes match the reviewed source |

The final CI and draft review used commit **a08b59b**. The immutable application
tag remains on **676bec2**, while native packages retain their separately recorded
packaging revision. Application C, kernel C, web assets, Makefile and VERSION
remain unchanged from the tagged source. Native package transaction checks are
recorded in the [package acceptance](native-packages-0.1.0.md).

The operator enabled +48V before recording; its immediate pre-recording state
was retained. Input 1 peaked at **−0.19 dBFS** without clipped samples. Slightly
lower GAIN 1 is recommended for more headroom. The initial read-only snapshot
preceded the operator's +48V change; it is not treated as a setting to restore.

Detailed metrics: [redacted audio report](pre-release-0.1.0.json). Voice files
and music remain private and are excluded from the repository and release.
No USB, suspend, host PipeWire restart, fresh DAW session or manual-to-native
package migration was performed in this final test. These results do not qualify
other rates, small buffers, other devices or long-session xrun behavior.

No blocking issue was found within the tested scope. The remaining release
action is publication of the reviewed stable draft.
