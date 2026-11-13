# MiniFuse 2 for Linux 0.1.1

Download the **application package** for your distribution:

- **CachyOS:** [minifuze2-0.1.1-1-cachyos-x86_64.pkg.tar.zst](https://github.com/Potato83/MiniFuze-for-Linux/releases/download/v0.1.1/minifuze2-0.1.1-1-cachyos-x86_64.pkg.tar.zst)
- **Arch Linux:** [minifuze2-0.1.1-1-arch-linux-x86_64.pkg.tar.zst](https://github.com/Potato83/MiniFuze-for-Linux/releases/download/v0.1.1/minifuze2-0.1.1-1-arch-linux-x86_64.pkg.tar.zst)
- **Ubuntu 24.04:** [minifuze2_0.1.1-1_amd64.deb](https://github.com/Potato83/MiniFuze-for-Linux/releases/download/v0.1.1/minifuze2_0.1.1-1_amd64.deb)

To use hardware switches and meters, also install the optional DKMS package:
[Arch/CachyOS](https://github.com/Potato83/MiniFuze-for-Linux/releases/download/v0.1.1/minifuze2-ctl-dkms-0.2.0-1-any.pkg.tar.zst) ·
[Ubuntu](https://github.com/Potato83/MiniFuze-for-Linux/releases/download/v0.1.1/minifuze2-ctl-dkms_0.2.0-1_all.deb).
Matching kernel headers and a separate setup step are required. If you already
installed the module manually, read the migration instructions first.

Install the application package with `sudo pacman -U ./FILE.pkg.tar.zst` or
`sudo apt install ./FILE.deb`. See the [installation and migration guide](https://github.com/Potato83/MiniFuze-for-Linux/blob/main/docs/packages.md).
There is no project package repository or automatic update service yet.

This version adds English and Russian desktop language selection and improves
readability in both themes. Change the language under Settings → Appearance →
Language and reopen the window. The fallback web panel remains in Russian;
system dialogs and some detailed errors may use their original language.

This is an independent application for the **Arturia MiniFuse 2**. It has been
physically tested with one device on CachyOS at 48 kHz with automatic buffer
settings. Other distributions, sample rates, small buffers and long sessions
need more testing. Please report issues with your distribution, kernel, PipeWire
version and steps to reproduce at [GitHub Issues](https://github.com/Potato83/MiniFuze-for-Linux/issues).
Remove serial numbers, private paths and audio from reports. Firmware updating
and physical MIDI DIN transfer remain unsupported or unverified.

<details>
<summary>SHA-256 checksums</summary>

```text
a7b3a81b6af633af1f07aebb86afe036f8ca405504bbb6c199f43d59afb42217  minifuze2-0.1.1-1-arch-linux-x86_64.pkg.tar.zst
3ab98f6e1cc3084724adfea50fd21609b9fbf25d39ed0e8414db4f285083043f  minifuze2-0.1.1-1-cachyos-x86_64.pkg.tar.zst
8991436c16cbcdfc5eea00f96b61a184712a59d1fb544fca18113d2e0a76b5de  minifuze2_0.1.1-1_amd64.deb
885425b3a6b8d12693ab30f7e56d8418785f4852d0df5a29334cbb552d700539  minifuze2-ctl-dkms-0.2.0-1-any.pkg.tar.zst
17d81ef9da9103a49df3878b0fec0476ef98ac1c3d49bfd3b1bfed565f4d6be7  minifuze2-ctl-dkms_0.2.0-1_all.deb
```

Run `sha256sum FILE` and compare the output with the corresponding value.
</details>
