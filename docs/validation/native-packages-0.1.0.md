# Native package acceptance for 0.1.0

Date: 2026-11-12. Application source is pinned to the original stable tag,
commit **676bec2bccc08bfa7ef6f645e5d09cfc6f1b3539**. Native packaging revision:
**ffb30ec**. Application version: 0.1.0-1; optional control package: 0.2.0-1.
The module's C source and application DSP are unchanged by this packaging work.
The packaged DKMS recipe explicitly selects the target kernel headers for
compatibility with both older and newer DKMS defaults.

| Check | Result | Evidence and limits |
| --- | --- | --- |
| Local software suite | Pass | 47 tests, including isolated runtime, package staging/refusal paths and release-supplement boundaries; no skipped tests |
| Ubuntu software/GTK/archive and kernel CI | Pass | [Build and test](https://github.com/Potato83/MiniFuze-for-Linux/actions/runs/43232679470); 47 software tests and the existing 31-stage isolated GTK suite |
| Arch Linux | Pass | Official Arch container: makepkg build, package dependency/ELF-version checks, desktop validation, installation/reinstallation/removal in a private pacman root |
| CachyOS | Pass | Official CachyOS container: separate binary build and the same private-root checks; generic x86-64 CPU flags and explicit OpenSSL soname dependencies |
| Ubuntu 24.04 | Pass | Real .deb build with dpkg-shlibdeps, payload inspection, apt installation/reinstallation/purge on the disposable runner, preset-file retention |
| Ubuntu DKMS | Pass | Packaged module installed for the running Azure kernel; explicit DKMS autoinstall also checked the separately installed generic target. No module loading or automatic device selection |
| AUR source bundles | Prepared | makepkg-generated source packages with .SRCINFO, checksum-pinned upstream and hashed local packaging assets; not submitted to AUR |
| Draft supplement | Pass | [Native package workflow](https://github.com/Potato83/MiniFuze-for-Linux/actions/runs/43232679467): all four jobs passed; all 18 final release assets were downloaded and checked |

The draft contains five native binary/source-DKMS packages, two AUR recipe
bundles, seven matching SHA256 sidecars and the four original archive/checksum
assets. Before editing the draft, its four original assets were downloaded and
hashed. After uploading packages and editing notes, all 18 assets were downloaded:
the original four matched their earlier bytes and asset identities, and the
native fourteen matched the checked build outputs. Draft browser URLs can change
when GitHub edits its notes; those URLs are not treated as content identities.
The original v0.1.0 tag remains on 676bec2 and the release remains unpublished.

Arch transaction checks bypass dependency installation and scripts/hooks inside
a private root; runtime libraries come from the matching build container. They
do not claim a real host DKMS migration. Ubuntu transaction checks run with
normal dependencies and DKMS maintainer scripts on an ephemeral hosted runner,
without a physical MiniFuse. SHA256 provides integrity checking, not package
signing or physical qualification of other distributions.

The physical CachyOS host retains its existing user-local application and manual
DKMS installation. No native package was installed over them. The separate
[kernel-upgrade observation](kernel-upgrade-0.1.0.json) checks the real boot into
7.3.4-1-cachyos, control and the 48 kHz graph. A later
[final audio check](pre-release-0.1.0.json) also passed headphone music and
microphone recording/replay. A fresh DAW test and a backed-up manual-to-package
migration remain separate tasks.

User instructions and migration boundaries: [package guide](../packages.md).
