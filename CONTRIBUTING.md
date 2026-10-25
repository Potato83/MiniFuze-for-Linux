# Contributing

This preview targets MiniFuse 2 on Linux. Read [TODO.md](TODO.md) before
starting work: MIDI DIN transfer, firmware updates and electrical calibration
are outside the current release claims.

## Reporting a problem

Include the application version, Linux distribution, CPU architecture, kernel,
PipeWire/WirePlumber versions, interface firmware, steps to reproduce, expected
behavior and actual behavior. State whether playback, recording or only the
control panel is affected. Remove serial numbers and personal paths from logs.
Do not upload Windows executables, proprietary drivers or private USB captures.

## Building and checking changes

Install the dependencies in [README.md](README.md), then run:

```sh
make -j2 all build/desktop-smoke
make check
```

The optional GTK integration check is documented in the README. It creates an
isolated software graph. Kernel compilation is separate (`make kernel`) and
requires matching kernel headers; compiling does not load the module. Do not
run installers as part of automated tests against your working system.

Keep new hardware operations bounded and document the evidence for selectors
and response formats. Distinguish protocol readback, digital tests and physical
measurements. Never infer electrical or audible behavior from a successful USB
status alone. Preserve ALSA ownership of the audio interfaces.

Keep [English](docs/reverse-engineering.md) and
[Russian](docs/reverse-engineering.ru.md) research reports in sync when findings
change. Include relevant validation and remaining limits in a pull request.

## Preparing a release

Run the build and checks above, check both GitHub Actions jobs after pushing,
and complete the publication checklist in TODO.md. The workflow tests Ubuntu
software behavior and module compilation; it does not qualify physical hardware.
The application preview is 0.1.0-preview; the independent kernel module version
is 0.2.0. These version numbers describe different components.

Only source files belong in Git. `build/`, `dist/` and machine-specific state
are ignored. Generate a source archive with `bash tools/package_release.sh`
only when an archive is needed. `--binary` includes executables linked against
the build system's libraries and is not a universal Linux package. Archive
generation does not publish a release.
