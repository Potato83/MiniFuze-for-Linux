# Attribution and licensing

This is an independent implementation for MiniFuse 2. Arturia and MiniFuse are
names of their respective owner. The project does not include Arturia logos,
firmware, installer files or proprietary program code.

The upstream [mf-cli](https://github.com/nolight132/mf-cli) by nolight132 was
an initial protocol reference. Its upstream snapshot is not bundled here.
Hardware selector values, USB observations and recovered behavior are recorded
as interoperability facts; new implementation sources have per-file SPDX notices.

The user-space C application, desktop, web UI, scripts and documentation are
provided under MIT unless a file says otherwise. The kernel implementation
and device-selection header are GPL-2.0-only. The UAPI header has the Linux
syscall exception. Full texts are supplied with this tree.

GTK, GLib, libsoup, json-c, libmicrohttpd, OpenSSL, PipeWire and Linux are
system dependencies distributed under their own licenses. Their code is not
vendored here. Packaging binaries together with third-party libraries would
need the corresponding additional notices; this source tree does not do that.

The preset fixtures are test data from controlled interoperability experiments.
Numeric reference vectors support regression tests; the research tools and
original Windows captures are kept outside this release tree.
