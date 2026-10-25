# Release checklist and deferred work

## Remaining functional coverage

- [ ] MIDI DIN IN/OUT transfer using a real cable; enumeration is insufficient.
- [ ] Firmware image validation, update protocol and interrupted-update recovery.
- [ ] Complete Windows parity for unclassified preset fields and all modes.
- [ ] C-service hotplug, suspend and long-session qualification beyond existing
  tests of the earlier implementation.
- [ ] Real kernel-upgrade/DKMS test and other distributions/interfaces.
- [ ] Physical input/plugin delay compensation and broader DAW/rate combinations.
- [ ] Small-buffer 32/64 qualification, prolonged load and USB-hub fault cause.
- [ ] Electrical calibration of +48V, levels and hardware meters.
- [ ] Desktop translations, accessibility review and HiDPI coverage on other themes.

The normal stream-based mixer remains the default. Restricted experimental
latency variants from the research workspace are not enabled in this release.
