# Installation and removal

## Application

Build with `make`, then run `bash tools/install_desktop.sh` as the desktop user.
It creates `~/.local/bin/minifuze2`, `~/.local/bin/minifuze2-engine`, a scalable
icon, `io.github.minifuze2.Control.desktop`, and a file manifest under
`~/.local/share/minifuze2/`. A new installation also creates and enables
`${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/minifuze2.service`.

`--reuse-existing` installs only the desktop and binaries. It does not alter
the earlier `minifuze-linux.service`, its state files, or its current engine.
This allows a reversible first test of the new window.

The engine service persists the saved preset and unsaved working mix separately.
Name edits are committed with Enter. Save stores the current preset; closing
the window keeps both the engine and its working mix alive.

## Kernel control endpoint

Standard audio uses ALSA. Extra hardware controls require the separate,
experimental `minifuze2_ctl` module. It provides bounded operations without
claiming USB interfaces, detaching ALSA/MIDI, resetting the device or exposing
arbitrary USB requests. Firmware writes are not available.

Install matching kernel headers and DKMS first. Review the module source and
select your own device serial and desktop account:

```sh
sudo bash tools/install_kernel.sh --serial YOUR_DEVICE_SERIAL --user YOUR_DESKTOP_USER
```

The installer checks input and refuses to overwrite an existing installation.
It installs version 0.2.0 through DKMS, serial-based selection, autoload and
a dedicated `minifuze2ctl` group containing the selected desktop user.
It does not unload a running module or restart audio. Group access applies
to a fresh login; the current endpoint owner is retained.

On an existing 0.2.0 installation, no kernel replacement is needed for this
desktop. Do not run the fresh installer over it.

## Removal

Close the application, then `bash tools/install_desktop.sh --uninstall`.
Its manifest must still match. This stops only the new `minifuze2.service`
if that service was installed by this script. Reused older services are retained.
State files remain available for backup or later reuse.

Kernel removal is separate. Verify each configuration belongs to this project
before deleting it:

```sh
sudo dkms remove -m minifuze2-ctl -v 0.2.0 --all
sudo rm /etc/modprobe.d/minifuze2-ctl.conf
sudo rm /etc/modules-load.d/minifuze2-ctl.conf
sudo rm /etc/udev/rules.d/70-minifuze2ctl.rules
sudo udevadm control --reload-rules
```

This does not remove a module already loaded in memory. Stop control clients
before explicitly unloading it. The dedicated group can be removed separately
after checking that it has no unrelated members or files.
