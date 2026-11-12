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

## Updating a managed installation

Close the desktop window, build the new version, then run:

```sh
bash tools/install_desktop.sh --update
```

The installer verifies the recorded hashes, updates the two binaries and icon,
and refreshes the manifest. It retains the desktop entry, any custom ready-file
argument, service configuration and user state. Only a service owned by this
installation is stopped/restarted, and only if it was active. A reused research
service is not restarted. If an update command fails, previous application files
and their manifest are restored. Reopen the desktop to use its new executable.
This does not replace the kernel module.

The new application reads earlier Linux state versions 1–3 and writes version 4.
Older applications cannot read version 4. Back up the state directory before
updating if you may need to return to an older engine.

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

If an older module was loaded with `device_path`, writing `device_serial`
to the modprobe configuration does not change that running instance. Audio
can recover in a new USB port while hardware buttons and hardware input meters
remain unavailable. The installer reports when the loaded selection differs.
Apply the saved selection on a later boot, or reload only `minifuze2_ctl` after
checking that a compatible module is available for the **running** kernel:

```sh
modinfo -k "$(uname -r)" -n minifuze2_ctl
```

Keep the current module loaded if this check fails. After a kernel package
upgrade, DKMS may have modules for the new kernel but not the old kernel still
running in memory. Boot a kernel with an installed module, or prepare a matching
module before attempting a reload. Reloading the control module does not require
reloading `snd_usb_audio` or restarting PipeWire. Device recreation also reapplies
the udev group rule; a session started before group membership was added may need
a fresh login to retain access.

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
