# How we investigated MiniFuse 2 and built a Linux control panel

This document records observable facts, experiments and the limits of our
conclusions. It is not a transcript of private reasoning: it keeps the
information needed to check the results and understand why some claims remain
unverified. Windows screenshots provided a feature checklist, but could not by
themselves show where signal processing happens or which protocol controls the
interface.

## Task and scope

MiniFuse 2's standard audio inputs and outputs already worked on Linux through
ALSA. The task was to find how the MiniFuse Control Center exposes additional
features: INST, Direct Mono, +48V, hardware loopback, three mixes, levels, pans,
meters, presets and audio settings.

The investigation covered one MiniFuse 2 with firmware 1.5.0. The result does
not certify every hardware revision, distribution, kernel or Arturia device.
The application does not contain Arturia firmware and does not replace the USB
audio driver. Real communication through the rear DIN MIDI ports and firmware
update/recovery are not implemented.

## How evidence was collected

We kept four kinds of evidence separate:

| Evidence | What it can confirm | What it cannot confirm by itself |
| --- | --- | --- |
| Windows Control Center screenshots and observation | Names, layout and visible control states | USB command format, audio path or exact fader scale |
| USB observation and static analysis of the application/driver | Requests, selectors, UI-to-parameter relationships and resource structures | That a command is safe in every state or audibly works |
| Device state read before and after an action | Which state changed and whether it was restored | Sound quality, level or absence of clicks/dropouts |
| Digital, cable and listening tests | Test-signal processing or audible behavior of a particular route | Analog-path calibration, every combination or long-term reliability |

The starting point was the open-source mf-cli 0.2.4 project
([nolight132/mf-cli](https://github.com/nolight132/mf-cli)), which already
contained some control commands. We then compared the Windows interface, the
device state before and after individual actions, USB traffic and MiniFuse
Control Center code. Preset format was studied by comparing files before and
after changes; numeric conversions were checked against small, isolated
functions from the Windows application.

The main research traces, Windows binaries and local state captures are not
included in the release. The public tree contains selected regression vectors
and presets for compatibility checks. This makes the project's tests
repeatable without publishing USB captures, serial numbers, personal files or
unverified dumps.

### Investigation sequence

1. Windows Control Center screenshots became a feature map: an Inputs screen
   for INST/+48V and virtual sources, an Outputs screen with three mixes, VU
   settings, firmware and ASIO settings. At this stage, names and appearance
   were observations, not proof that a feature worked.
2. mf-cli provided several known requests and a useful way to compare
   descriptors. We checked those findings against Windows code and device
   state until the set of allowed switches and readback operations was narrow
   enough for a safe Linux interface.
3. We found that the large mixer screen mainly describes a software matrix and
   saved state, not a ready-made DSP operation sent in one USB command. We
   therefore studied coefficients, Windows presets and actual signal flow on
   Linux separately.
4. Each important claim was split into three questions: what the interface
   reports, what a file or device stores, and what happens to the audio signal.
   That kept a simple bit change from becoming a premature claim about an
   audible route or calibrated level.
5. An early attempt to control the device by taking ownership of a USB
   interface interfered with ALSA. We changed the design: standard audio stayed
   with the normal driver, while a kernel module kept only a bounded set of
   control transfers. PipeWire, shared-rate settings and DAW checks were
   separated in the same way.
6. After collecting numeric reference values and hardware observations, we
   moved the main implementation to C and built a GTK window. Measurements
   from the earlier panel were not retroactively presented as a full test of
   the new C preview.

## Two separate paths: audio and control

The first important distinction is that streamed audio and additional control
commands are different things. Ordinary playback and recording use the
Linux USB audio driver and ALSA. The MiniFuse Linux application does not take
over the audio interface or implement a replacement USB audio driver.

The additional Control Center switches use short USB control transfers. An
early prototype that claimed the whole USB interface interfered with normal
ALSA operation. The working version uses a separate, small kernel module: it
selects the requested card, performs only explicitly listed operations,
temporarily acquires a reference to the USB device and a runtime-PM reference,
checks response lengths, then releases the device. The audio driver stays
bound.

### What we established about USB commands

For a switch, the Linux client sends a class/interface control request 0x22:
bmRequestType 0x21 (host-to-device, class, interface), wValue is the
selector, wIndex is 0 and wLength is 2. Data is a little-endian uint16:
01 00 for on and 00 00 for off. It then reads state with request 0xA2:
bmRequestType 0xA1 (device-to-host, class, interface), the same wValue,
wIndex 0 and expected length 2 bytes. Both requests use control endpoint 0.
The kernel implementation sets a 1000 ms timeout for each transfer; the
application does not retry. The driver rejects short or long responses. A
write is considered successful only if readback matches; USB status alone is
not enough.

| Function | Selector |
| --- | ---: |
| INST 1 / INST 2 | 0x0000 / 0x0001 |
| +48V | 0x0400 |
| Direct Mono | 0x0500 |
| Loopback / USB 1–2 / Monitor | 0x1300 / 0x1400 / 0x1500 |

Request 0xB2 returns a 46-byte packed state. Reading it independently lets
us see which bit changed when a single mode is enabled. It has
bmRequestType 0xA1, bRequest 0xB2, zero wValue/wIndex, and wLength 46;
the 46 bytes contain 23 little-endian uint16 words. Static analysis of the
Windows handler mapped the words as follows:

| Words (zero-based) | Static interpretation |
| --- | --- |
| 0, bits 0 and 1 | INST 1 and INST 2 |
| 1 | Raw input type |
| 2–3 | Raw analog-input VU values |
| 4, bit 0 | +48V |
| 5, bit 0 | Direct Mono |
| 6–11 and 12–17 | Raw int16 Loopback Left and Right levels |
| 18, bits 0–5 | Loopback mute slots |
| 19, 20 and 21, bit 0 | Loopback, USB 1–2 and Monitor routing |
| 22 | Raw output level |

An independent runtime diff confirmed the expected words for Direct Mono (5),
+48V (4) and the three routing flags (19–21). The rest of the table comes from
static analysis; it does not claim that all raw levels were measured in dB.

Request 0xC2 has bmRequestType 0xA1, zero wValue/wIndex and a length of
16 bytes. Windows interprets the response as four little-endian signed int32
values at offsets 0, 4, 8 and 12: Input 1, Input 2, Loopback Left and Loopback
Right. The Windows handler applies a signed arithmetic shift of 8 bits to each
value and then scales it as float32. On Linux we compared channel mapping
and response to a signal, but did not calibrate it electrically in dBFS/dBu or
measure connector voltage.

Selectors were not guessed from button order: they were matched against
Windows UI/driver properties, device state and readback. The public module
allows only known switches. It has no arbitrary USB request, unknown-selector
write, reset, ALSA disconnection or firmware-transfer interface.

Before an operation, the module checks VID:PID 1c75:af90 and exactly one
selection criterion: USB path or serial number. A path ties control to one
port; a serial number follows the same interface when moved to another port.
No matches return ENODEV; multiple matches return an ambiguity error. The
module does not claim the audio interface or detach it from ALSA.

+48V was tested only with both XLR inputs free. The command and device state
were switched and restored, but voltage at the contacts was not electrically
measured, and microphone operation under phantom power is not claimed.

## Reconstructing the Windows mixer

The Control Center Outputs screen shows three separate stereo mixes: Monitor
& Phones, Loopback Mix 3/4 and Loopback Mix 5/6. Sources are two mono analog
inputs and four stereo application pairs: Main, Virtual 3/4, 5/6 and 7/8.
The software model therefore has ten mono sources feeding three two-channel
mixes—a 10 × 6 matrix.

Each strip has a level, mute and pan; each mix also has a master level. A
virtual stereo strip keeps a separate pan for each source channel. Each matrix
coefficient combines the strip level, pan, mute state and mix master level.
This is the software model; it does not claim that hardware Monitor/Phones and
loopback are the same DSP.

Static analysis and numeric calls to the original function showed a sine/cosine
pan law, a separate gain multiplier and a signed int16 level in Q8 dB, with
1/256 dB steps. Mute sets the coefficient to zero independently of level.
Several internal numeric vectors were compared against the original function
in an isolated harness. This did not run the Windows GUI, driver or USB; pan
extremes and every summing behavior have not been exhaustively measured.

Reconstruction was not limited to visually similar controls. Reference
vectors are used in C regression tests. The public test compares each matrix
coefficient against a saved reference with an absolute coefficient tolerance
of 1.3×10⁻⁷. These checks cover the digital model, not the analog DAC or sound
quality.

## .mfp format and fader accuracy

A Windows .mfp file stores mixer audio parameters together with names,
visibility settings, strip links and other fields. The supported slice handles
69 audio parameters across the three mixes. Each mix contains two mono inputs
with Level/Mute/one Pan coordinate, four stereo strips with Level/Mute/two Pan
coordinates, and one master level: 2×3 + 4×4 + 1 = 23 parameters; three
mixes make 69. Unknown fields are copied only from the source .mfp template;
the converter does not invent them and does not guarantee understanding of
every field in an arbitrary version or file. We checked the factory fixture
and selected Windows round-trip fixtures, not every third-party preset.

The factory fixture contains 142 parameters. Import and export preserve all
142. A Windows import → export → Linux reimport cycle confirmed matching values
for all 69 audio parameters: level, mute and pan fields were compared; mute
and pan matched exactly, and the maximum absolute level difference was about
0.00000382 dB. In one experiment Windows removed an unused name for an
additional USB source. This is recorded as an observation rather than hidden
behind a claim that the files were completely identical.

In the original resource, normalized level maps exponentially to a range from
−60 to 0 dB. For x from 0 to 1, the formula is:

    a = −2, b = 1 / (exp(a) − 1), u = (exp(a·x) − 1)·b
    dB = −60 + 60·u

The original program performs intermediate operations in float32. Moving
the formula to double and rounding only at the end does not guarantee a
match. For example, x = 0.5 gives about −16.1364861 dB. Numeric vectors were
compared against pure functions from the Windows binary in a restricted
harness; the application entry point, constructors, GUI, USB and driver were
not run.

PeakRef is separate scale metadata, not an ordinary fader. Link ties level,
mute and reference for an analog pair, while pan remains independent; Show
controls virtual-strip visibility. These behaviors were compared with Windows
using controlled presets. Unknown fields are preserved, but not all have a
dedicated Linux control.

## Testing the physical interface

For each hardware switch, we compared at least two sources: selector A2
readback and packed state B2. For a one-flag operation, we checked that the
expected word/bit changed and then restored the original state. Before testing
+48V, we confirmed that no microphone was connected to XLR.

Direct Mono passed listening tests with a guitar and headphones, separately
for each analog input:

| Source | Mono off | Mono on | After switching off |
| --- | --- | --- | --- |
| Input 1 | Signal in left ear only | Both ears | Left ear only again |
| Input 2 | Signal in right ear only | Both ears | Right ear only again |

This confirms the observed channel distribution under these conditions. The
test did not measure phase, level when two signals are present at once, analog
gain or complete absence of clicks.

Monitor/Phones was checked with short, low-amplitude tones: the baseline USB
output played separate tones left and right; hardware loopback combined with
monitor produced an audible return; individually tested flags without the
required route were silent. Rear OUTPUT L/R were also connected by cable to
each analog input to test the physical paths separately. These results are not
a complete table of every switch and level combination.

For hardware VU, we compared raw C2 responses, a guitar input and digital
loopback. Four meter channels were mapped, but their electrical calibration
and exact scale relative to dBFS remain open.

For analog-path checks, rear OUTPUT L and R were connected in turn by a 6.35 mm
cable to INPUT 1 and INPUT 2. Signal return confirmed these four physical
paths under the tested conditions. OUTPUT L → INPUT 2 was separately checked
at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. This was not a calibrated test bench
and does not prove identical amplitude response for all levels, frequencies,
buffers or cables.

## PipeWire, sample rate and latency

The Linux application creates its own software matrix and routes in PipeWire,
while ALSA continues to serve the USB audio device. Sample rate and quantum
(period/buffer size) belong to the shared audio service. They are not settings
for this interface alone: forcing a rate can affect other devices too.

We checked the requested and actual PCM/PipeWire state, not just the value in
the window. Supported sample rates are 44.1, 48, 88.2, 96, 176.4 and 192 kHz;
Auto hands selection back to the service. An early forced-mode experiment
degraded the laptop's built-in speakers; returning to Auto restored normal
sound. The application therefore checks whether a shared mode can be applied
safely and does not claim that changing the rate is isolated from the rest of
the system.

Latency was tested separately from SET/GET operations: digital signals, an
OUTPUT → INPUT cable loop and limited Ardour projects cover different parts of
the path. One confirmed Ardour route—Virtual 3/4 → Mix 3/4—passed separate
alignment tests; experimental narrow profiles are not part of the normal
configuration. Results from the earlier research service cannot automatically
be treated as qualification of the new C preview.

During the C port, another class of bug appeared and was fixed: reading the
meter stream waited for a shared mutex while the application checked whether
a buffer change had taken effect. The reader stopped draining a pipe and the
graph stalled. The fix keeps draining the stream, publishes data when the lock
is available and preserves peaks between publications. Repeated 48 kHz / 512
frame transitions passed after the fix. This is why a successful settings
response alone is not proof that the audio graph is working.

## From research to the application

The current preview is split into a native GTK 4 window, a separate C
engine/service, a local web fallback and a bounded kernel module for additional
switches. Audio runs through ALSA/PipeWire; the mixer service continues to run
when the panel is closed. kernel/minifuze2_ctl.c and
kernel/minifuze2_uapi.h show the hardware interface boundary;
native/app/ contains the model, matrix, presets, meters and graph;
native/desktop/ contains the GTK client.

Build dependencies and environment setup are listed in the README. From the
unpacked release root, public checks include:

- tests/fixtures/native-port-reference.json — numeric matrices and test
  positions for meter marks;
- tests/fixtures/mf2-*.mfp — controlled factory and Windows round-trip
  presets;
- make check — source, numeric, preset and safety checks;
- python3 tools/test_desktop.py — GTK test on an isolated graph, without
  physical audio routes or changes to the user's mix.

These checks cover the software slice and prepared fixtures. Repeating the
hardware checks still requires a MiniFuse 2 and suitable safe conditions.

## Platform compatibility

| Target | Status |
| --- | --- |
| CachyOS, x86_64, kernel 7.3.0-2-cachyos, PipeWire 1.6.9, MiniFuse 2 firmware 1.5.0 | Built and tested on one physical interface |
| Ubuntu 24.04 | Ubuntu CI build and isolated software tests are configured; first GitHub run pending, no physical MiniFuse qualification |
| Other Linux distributions | Expected to be portable when GTK 4.12+, libsoup 3, the listed development libraries, PipeWire and WirePlumber are available; hardware and installer behavior still need distribution-specific testing |
| Linux kernel control module | Linux only; requires headers for the target kernel. DKMS support and kernel API compatibility vary by distribution/kernel |
| Windows and macOS | Not supported by this application |

GTK itself is available on Windows and macOS
([GTK platform documentation](https://docs.gtk.org/gtk4/)). That does not make
this application portable: its engine uses Linux
[PipeWire](https://pipewire.org/)/ALSA interfaces, Linux /proc and /sys device
data, and its optional hardware controls use a Linux kernel module built for
the target kernel ([kernel module documentation](https://docs.kernel.org/kbuild/modules.html)).
The desktop installer also uses a systemd user service and the hardware-control
setup uses udev. A Windows or macOS version would need new audio, device-control
and installation backends; rebuilding the GTK window alone would not be enough.
The source archive and binaries are not universal packages.

## What is not claimed yet

The project does not promise feature parity with every version of Windows
Control Center. Open areas include:

- real MIDI DIN IN/OUT communication with an external cable and MIDI device;
- firmware update, image validation and recovery after interruption;
- electrical calibration of +48V, input level and hardware VU;
- long-duration testing of all physical routes, rates and small buffers;
- broad coverage of DAWs, plugins, devices and Linux distributions;
- every unknown .mfp field, Windows mode and DSP boundary value;
- the C service through arbitrary suspend/resume cycles and long workloads.

The current list of limitations is in [TODO.md](../TODO.md). Feature and build
summary: [English README](../README.md) and [Russian README](../README.ru.md).
Installation and kernel-module boundaries are documented in
[installation.md](installation.md).

## Source index

| Area | Public files |
| --- | --- |
| Hardware-switch protocol | kernel/minifuze2_ctl.c, kernel/minifuze2_uapi.h |
| Interface selection | kernel/minifuze2_selection.h |
| Mixer state and matrix | native/app/model.c, native/app/mf.h |
| .mfp import/export | native/app/preset.c and the section above |
| PipeWire, devices and routes | native/app/graph.c, native/app/audio.c, native/app/device.c |
| Meters and hardware readings | native/app/meters.c |
| Desktop window | native/desktop/ |
| Preview safety boundaries | README.md, docs/installation.md, TODO.md |

Detailed evidence remains in the source and restricted fixtures. Before
publishing, do not add personal USB captures, Windows application/driver
images, serial numbers or user files to the repository.
