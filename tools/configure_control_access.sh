#!/usr/bin/env bash
# Assign this control endpoint to one dedicated system group and desktop user.
set -euo pipefail
account=''
dry_run=0
while (($#)); do
    case "$1" in
        --user) account=${2:?Missing account}; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        *) echo "Usage: $0 --user ACCOUNT [--dry-run]" >&2; exit 2 ;;
    esac
done
[[ $account =~ ^[a-z_][a-z0-9_-]{0,31}$ ]] || { echo 'Invalid account name' >&2; exit 2; }
account_uid=$(id -u "$account")
[[ $account_uid != 0 ]] || { echo 'Select the desktop account, not root' >&2; exit 2; }
group=minifuze2ctl
rule=/etc/udev/rules.d/70-minifuze2ctl.rules
old_rule=$(printf 'SUBSYSTEM=="misc", KERNEL=="minifuze2ctl", OWNER="%s", MODE="0600"' "$account")
new_rule='SUBSYSTEM=="misc", KERNEL=="minifuze2ctl", GROUP="minifuze2ctl", MODE="0660"'
if [[ -L $rule ]]; then echo 'Refusing a symlink rule' >&2; exit 1; fi
if [[ -e $rule ]]; then
    content=$(cat -- "$rule")
    [[ $content == "$old_rule" || $content == "$new_rule" ]] || { echo 'Existing rule differs; inspect before replacing' >&2; exit 1; }
fi
existing=$(getent group "$group" || true)
if [[ -n $existing ]]; then
    IFS=: read -r group_name group_password group_id group_members <<< "$existing"
    # Do not repurpose an ordinary user's group or grant access to others.
    [[ $group_id =~ ^[0-9]+$ && $group_id -lt 1000 ]] || { echo 'Existing group is not a system group' >&2; exit 1; }
    [[ -z $group_members || $group_members == "$account" ]] || { echo 'Existing group has other members' >&2; exit 1; }
    extra_primary=$(getent passwd | awk -F: -v gid="$group_id" -v wanted="$account" '$4 == gid && $1 != wanted {print $1}')
    [[ -z $extra_primary ]] || { echo 'Other accounts use this primary group' >&2; exit 1; }
fi
printf 'Control endpoint group: %s\nOnly desktop member: %s\nRule: %s\n' "$group" "$account" "$rule"
if ((dry_run)); then exit 0; fi
[[ $EUID == 0 ]] || { echo 'Run the reviewed access configuration with sudo' >&2; exit 1; }
if [[ -z $existing ]]; then groupadd --system "$group"; fi
usermod -a -G "$group" "$account"
printf '%s\n' "$new_rule" | install -o root -g root -m 0644 /dev/stdin "$rule"
udevadm verify "$rule"
udevadm control --reload-rules
# Keep the existing owner, so the current session needs no new login. Future
# device creation uses root:group; the next login picks up group membership.
if [[ -c /dev/minifuze2ctl ]]; then
    chgrp "$group" /dev/minifuze2ctl
    chmod 0660 /dev/minifuze2ctl
fi
printf 'Configured. Current endpoint owner retained; group membership persists for the next login.\n'
