# Local validation of 0.1.0-rc.1

The following checks were run locally on CachyOS x86_64. No physical audio
interface was used for the studio candidate tests. PipeWire and WirePlumber
ran in private runtime directories; WirePlumber 0.5 used its policy-only
profile and the engine used a nonexistent control-device path.

- `dbus-run-session -- env MINIFUZE_RUNTIME_TESTS=1 make check`: 28 tests
  passed, with no skips. Coverage includes DSP coefficients, legacy formats,
  Linux presets, service restart, Undo/Redo and isolated installer updates.
- The GTK smoke test completed all 31 stages, including source and master
  controls, numeric gain, presets, metadata, themes, small-window layout,
  offline state and recovery from a rejected CSRF token.
- The same 31 GTK stages passed with AddressSanitizer and UndefinedBehaviorSanitizer
  enabled. Leak detection was disabled because GTK keeps process-global state;
  this result is not a leak audit.
- The embedded browser interface was exercised against a private engine:
  Solo, Dim, renaming and Undo completed without browser errors.
- The separate kernel module compiled against 7.3.1-1-cachyos headers.
  It was not loaded as part of these checks.
- JavaScript syntax, shell syntax and Git whitespace checks passed.

The screenshots in `docs/images/` come from the isolated GTK test. They show
the real application without a physical MiniFuse connection.

Additional physical loopback, same-port USB reconnect, switch readback and
real user-service update results are documented in
[hardware acceptance](../hardware-validation.md). Listening, DAW use,
suspend recovery and broader configurations remain outside these checks.
Ubuntu GitHub Actions and publication are tracked on the
[release checklist](../../TODO.md).
