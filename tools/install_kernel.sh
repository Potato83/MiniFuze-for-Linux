#!/usr/bin/env bash
# Install only the bounded MiniFuse control module; never reload running audio.
set -euo pipefail

serial=''
account=''
dry_run=0
while (($#)); do
    case "$1" in
        --serial) serial=${2:?Missing serial}; shift 2 ;;
        --user) account=${2:?Missing account}; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        *) echo "Usage: $0 --serial USB_SERIAL --user ACCOUNT [--dry-run]" >&2; exit 2 ;;
    esac
done
[[ $serial =~ ^[A-Za-z0-9_.:-]{1,127}$ ]] || { echo 'Invalid USB serial' >&2; exit 2; }
[[ $account =~ ^[a-z_][a-z0-9_-]{0,31}$ ]] || { echo 'Invalid account name' >&2; exit 2; }
account_uid=$(id -u "$account")
[[ $account_uid != 0 ]] || { echo 'Select the desktop account, not root' >&2; exit 2; }
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
package=minifuze2-ctl
version=0.2.0
source_dir=/usr/src/$package-$version
modprobe_file=/etc/modprobe.d/minifuze2-ctl.conf
autoload_file=/etc/modules-load.d/minifuze2-ctl.conf
udev_file=/etc/udev/rules.d/70-minifuze2ctl.rules
kernel_release=$(uname -r)
files=(Makefile minifuze2_ctl.c minifuze2_selection.h minifuze2_uapi.h dkms.conf)
for file in "${files[@]}"; do
    [[ -f $repo/kernel/$file ]] || { echo "Missing source: $file" >&2; exit 1; }
done
for utility in dkms make install udevadm modinfo groupadd usermod getent; do
    command -v "$utility" >/dev/null || { echo "Missing dependency: $utility" >&2; exit 1; }
done
[[ -d /usr/lib/modules/$kernel_release/build ]] || { echo 'Matching kernel headers required' >&2; exit 1; }
# Refuse to overwrite any previous package/configuration, including symlinks.
for target in "$source_dir" "/var/lib/dkms/$package/$version" "$modprobe_file" "$autoload_file" "$udev_file"; do
    [[ ! -e $target && ! -L $target ]] || { echo "Already exists; inspect before updating: $target" >&2; exit 1; }
done
printf 'Package: %s/%s\nKernel: %s\nSerial: %s\nOwner: %s (uid %s)\n' "$package" "$version" "$kernel_release" "$serial" "$account" "$account_uid"
printf 'Create: %s\n' "$source_dir" "$modprobe_file" "$autoload_file" "$udev_file"
printf 'Running module, ALSA, mixer and PipeWire clock settings are retained.\n'
bash "$repo/tools/configure_control_access.sh" --user "$account" --dry-run
if ((dry_run)); then
    exit 0
fi
[[ $EUID == 0 ]] || { echo 'Run the reviewed installer with sudo' >&2; exit 1; }

install -d -m 0755 "$source_dir"
for file in "${files[@]}"; do
    install -o root -g root -m 0644 "$repo/kernel/$file" "$source_dir/$file"
done
# These commands affect this package only. Configs are written after a successful
# build/install, so a build failure cannot enable an unbuilt module at startup.
dkms add -m "$package" -v "$version"
dkms build -m "$package" -v "$version" -k "$kernel_release"
dkms install -m "$package" -v "$version" -k "$kernel_release" --directive 'modprobe_on_install='
printf 'options minifuze2_ctl device_serial=%s\n' "$serial" | install -o root -g root -m 0644 /dev/stdin "$modprobe_file"
printf 'minifuze2_ctl\n' | install -o root -g root -m 0644 /dev/stdin "$autoload_file"
bash "$repo/tools/configure_control_access.sh" --user "$account"
printf 'Installed. The current module was not unloaded or replaced in memory.\n'
dkms status -m "$package" -v "$version"
modinfo -n minifuze2_ctl
