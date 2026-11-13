# MiniFuse 2 for Linux 0.1.1

An independent Linux mixer for **Arturia MiniFuse 2**. This test release adds
English and polishes the compact GTK desktop. We welcome reports from people
using different Linux distributions and kernels.

## What's new

- English and Russian desktop interface. The language follows the system by
  default and can be selected under Settings → Appearance → Language. Reopen
  the application window to apply a change; the audio service keeps running.
- Larger secondary labels and fader handles, stronger strip headings and fixed
  label contrast when switching between the dark and light themes.
- Language and theme choices persist independently of mixer settings. The
  fallback web panel remains in Russian. System file dialogs and some detailed
  system or service errors may use their original language.

## Downloads

Application packages are provided for Ubuntu 24.04 amd64, Arch Linux x86_64
and CachyOS x86_64. Download the package matching your system and its `.sha256`
file; verify it with `sha256sum -c FILE.sha256`, then install it with
`sudo apt install ./FILE.deb` or `sudo pacman -U ./FILE.pkg.tar.zst`.
The optional hardware-control DKMS module is version 0.2.0 and is a separate
package. AUR-ready source recipes and Ubuntu/source archives are also included.
There is currently no project package repository or automatic update service.
See the [package guide](https://github.com/Potato83/MiniFuze-for-Linux/blob/main/docs/packages.md)
for migration from a manual installation and module setup.

## Testing and limitations

The interface and mixer were tested with isolated GTK and audio integration
checks in both languages; the updated window was reviewed by the project owner.
Physical playback, recording, controls and Ardour were previously checked with
one MiniFuse 2 on CachyOS, at 48 kHz and automatic buffer settings. Those
checks do not establish long-session reliability, other rates or small buffers,
or physical compatibility with Ubuntu and other distributions.

Please report installation problems, crashes, audio issues, language/layout
problems and steps to reproduce through [GitHub Issues](https://github.com/Potato83/MiniFuze-for-Linux/issues).
Include your distribution, kernel, PipeWire version and MiniFuse model. Remove
serial numbers, personal paths and private audio from any material you share.
Firmware updates and MIDI DIN transfer remain unsupported or unverified. This
is an independent project and is not affiliated with Arturia.
