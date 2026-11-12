# Preparing and publishing a release

`VERSION` names the application; `kernel/dkms.conf` independently names the
control module. A stable version is prepared with a bounded support claim:
MiniFuse 2, the tested firmware/platform and 48 kHz with automatic clock settings.
Broader rates, small buffers and additional machines remain qualification work.

## Before tagging

1. Update `VERSION`, `CHANGELOG.md`, both READMEs, `docs/release-notes.md` and `TODO.md`.
2. Run `make -j2 all build/desktop-smoke`, then `dbus-run-session -- env MINIFUZE_RUNTIME_TESTS=1 make check` and the isolated GTK check described in the README.
3. Compile the module against available target headers in a separate source copy. Do not unload a working module to check compilation.
4. Record installed-update and physical acceptance evidence with serials, personal paths and recordings omitted. Preserve the distinction between the published candidate and any source fixes installed later.
5. Commit and push the preparation branch. Both Ubuntu CI jobs must pass for the commit selected for release. Every branch build uploads source/binary archives for review.
6. Review the final notes, archive contents and known limits. Complete the release acceptance items in `TODO.md`.

## Local archives

Packaging requires Python 3 (standard library), Git for a checkout, and `strip`
for a binary archive. The installed application requires neither Python nor Git.
A Git checkout must have no tracked changes. Untracked files are excluded.
A verified extracted source archive can also be packaged without Git.

```sh
bash tools/package_release.sh
MF_PACKAGE_PLATFORM=cachyos-x86_64 bash tools/package_release.sh --binary
python3 tools/release.py verify --version "$(cat VERSION)" dist/*.tar.gz
```

Keep old outputs. `--output-dir /absolute/path` selects a different destination;
existing archives are never overwritten. Source content, permissions, timestamps
and compression are normalized so the same source produces identical archives.
Binary contents still depend on the build toolchain and system libraries.
`BUILD-INFO.json` records the application/module versions, source revision,
source date and platform. Every file is covered by `SHA256SUMS`; a sidecar covers
each archive. These are integrity checks, not signatures or hardware certification.
The packager checks both executable versions before including them.

## GitHub release

After the review, push a tag exactly matching `v` plus `VERSION`. The tag workflow
runs the same Ubuntu application/GTK/archive and kernel jobs. It validates the
archive manifests, versions and release-note heading before creating a release.

- A version such as `0.1.0-rc.2` creates a public **prerelease**.
- A version such as `0.1.0` creates a **draft stable release**, with its archives and notes ready for final review.

For the stable release, inspect the draft in GitHub Releases, confirm the two
archives and sidecars, then publish that draft. Until publication the previous
release remains the public download. Do not retarget a published tag or replace
published archives; fix a discovered problem in a new version.
