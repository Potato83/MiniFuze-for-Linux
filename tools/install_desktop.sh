#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# User-local installation. Kernel, clocks, ALSA and default devices are untouched.
set -eEuo pipefail
[[ $EUID != 0 ]] || { echo 'Run as the desktop user, without sudo' >&2; exit 1; }
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
base=${HOME:?HOME is required}/.local
config=${XDG_CONFIG_HOME:-$HOME/.config}
reuse=false
remove=false
update=false
ready_file=''
while (($#)); do
  case "$1" in
    --reuse-existing) reuse=true; shift ;;
    --ready-file) ready_file=${2:?Missing ready file}; shift 2 ;;
    --prefix) base=${2:?Missing prefix}; shift 2 ;;
    --config-home) config=${2:?Missing config directory}; shift 2 ;;
    --uninstall) remove=true; shift ;;
    --update) update=true; shift ;;
    *) echo 'Usage: install_desktop.sh [--reuse-existing --ready-file FILE] [--prefix DIR --config-home DIR] [--update | --uninstall]' >&2; exit 2 ;;
  esac
done
if $update && { $remove || $reuse || [[ -n $ready_file ]]; }; then
  echo '--update preserves the installed service and desktop configuration; do not combine it with other modes.' >&2; exit 2
fi
for path in "$base" "$config"; do
  [[ $path == /* && $path != / ]] || { echo 'Use absolute installation directories' >&2; exit 1; }
done
for path in "$base" "$config" "$ready_file"; do
  case "$path" in *'$'*|*'%'*|*'"'*|*'\'*|*'`'*|*$'\n'*|*$'\r'*) echo 'Unsupported path characters' >&2; exit 1 ;; esac
done
manifest="$base/share/minifuze2/installed.sha256"
owned="$base/share/minifuze2/owned-files"
unit="$config/systemd/user/minifuze2.service"
targets=("$base/bin/minifuze2" "$base/bin/minifuze2-engine"
  "$base/share/icons/hicolor/scalable/apps/io.github.minifuze2.Control.svg"
  "$base/share/applications/io.github.minifuze2.Control.desktop")
refresh_menu() {
  if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$base/share/applications" || echo 'Desktop cache refresh failed; application files remain valid.' >&2
  fi
}
if $remove || $update; then
  [[ -f $manifest && -f $owned && ! -L $manifest && ! -L $owned ]] || { echo 'Installation manifest missing' >&2; exit 1; }
  mapfile -t recorded < "$owned"
  [[ ${#recorded[@]} == 4 || ${#recorded[@]} == 5 ]] || { echo 'Incomplete installation manifest' >&2; exit 1; }
  for i in "${!targets[@]}"; do
    [[ ${recorded[$i]} == "${targets[$i]}" ]] || { echo 'Unknown manifest path/order' >&2; exit 1; }
  done
  if ((${#recorded[@]} == 5)); then
    [[ ${recorded[4]} == "$unit" ]] || { echo 'Unexpected unit path' >&2; exit 1; }
  fi
  expected=$(sha256sum -- "${recorded[@]}")
  [[ $(cat -- "$manifest") == "$expected" ]] || { echo 'Installed files changed; inspect before removing' >&2; exit 1; }
fi
if $remove; then
  if ((${#recorded[@]} == 5)); then
    systemctl --user disable --now minifuze2.service
  fi
  rm -- "${recorded[@]}" "$manifest" "$owned"
  if ((${#recorded[@]} == 5)); then systemctl --user daemon-reload; fi
  refresh_menu
  echo 'Removed application files. Presets and kernel module retained.'
  exit
fi
[[ -x $repo/build/minifuze2 && -x $repo/build/minifuze2-engine ]] || { echo 'Build first: make' >&2; exit 1; }
if $update; then
  backup=$(mktemp -d "$repo/build/.desktop-update.XXXXXXXX")
  active=false
  if ((${#recorded[@]} == 5)) && systemctl --user is-active --quiet minifuze2.service; then active=true; fi
  for i in "${!recorded[@]}"; do cp -p -- "${recorded[$i]}" "$backup/$i"; done
  cp -p -- "$manifest" "$backup/manifest"
  committed=false
  stopped=false
  replacement=''
  replace_file() {
    local source=$1 target=$2 mode=$3
    replacement=$(mktemp "$(dirname -- "$target")/.minifuze-update.XXXXXXXX")
    cat -- "$source" > "$replacement"
    chmod "$mode" "$replacement"
    mv -f -- "$replacement" "$target"
    replacement=''
  }
  rollback_update() {
    local result=$?
    trap - EXIT INT TERM
    [[ -z $replacement ]] || rm -f -- "$replacement"
    if ! $committed; then
      for i in "${!recorded[@]}"; do
        replace_file "$backup/$i" "${recorded[$i]}" "$(stat -c %a -- "$backup/$i")"
      done
      replace_file "$backup/manifest" "$manifest" 600
      if $active && $stopped; then systemctl --user start minifuze2.service || true; fi
      echo 'Update failed; previous application files restored.' >&2
    fi
    rm -rf -- "$backup"
    exit "$result"
  }
  trap rollback_update EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  if $active; then
    systemctl --user stop minifuze2.service
    stopped=true
  fi
  replace_file "$repo/build/minifuze2" "${targets[0]}" 755
  replace_file "$repo/build/minifuze2-engine" "${targets[1]}" 755
  replace_file "$repo/packaging/io.github.minifuze2.Control.svg" "${targets[2]}" 644
  sha256sum -- "${recorded[@]}" > "$backup/new-manifest"
  replace_file "$backup/new-manifest" "$manifest" 600
  if $active; then systemctl --user start minifuze2.service; fi
  committed=true
  echo 'Updated application files. Presets, working mix and service configuration retained. Reopen the desktop window.'
  exit
fi
$reuse || targets+=("$unit")
for target in "${targets[@]}" "$manifest" "$owned"; do
  [[ ! -e $target && ! -L $target ]] || { echo "Already exists; inspect before replacing: $target" >&2; exit 1; }
done
if ! $reuse && systemctl --user is-active --quiet minifuze-linux.service; then
  echo 'Existing MiniFuse service found. Use --reuse-existing to avoid duplicate graphs.' >&2; exit 1
fi
if [[ -n $ready_file ]]; then
  $reuse && [[ $ready_file == /* && -f $ready_file ]] || { echo 'Use an existing absolute ready-file path with --reuse-existing' >&2; exit 1; }
fi
plan=$(mktemp -d "$repo/build/.desktop-install.XXXXXXXX")
created=()
enable_attempted=false
committed=false
cleanup() {
  local result=$?
  trap - EXIT INT TERM
  if ! $committed; then
    if $enable_attempted; then systemctl --user disable minifuze2.service || true; fi
    for ((i=${#created[@]}-1;i>=0;i--)); do rm -f -- "${created[$i]}"; done
    if $enable_attempted; then systemctl --user daemon-reload || true; fi
  fi
  rm -rf -- "$plan"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
{
  printf '[Desktop Entry]\nType=Application\nName=MiniFuse 2 for Linux\nComment=MiniFuse mixer and interface controls\n'
  printf 'Exec="%s/bin/minifuze2"' "$base"
  [[ -z $ready_file ]] || printf ' --ready-file "%s"' "$ready_file"
  printf '\nIcon=io.github.minifuze2.Control\nTerminal=false\nCategories=AudioVideo;Audio;Mixer;\nStartupNotify=true\nStartupWMClass=io.github.minifuze2.Control\n'
} > "$plan/io.github.minifuze2.Control.desktop"
if command -v desktop-file-validate >/dev/null; then desktop-file-validate "$plan/io.github.minifuze2.Control.desktop"; fi
if ! $reuse; then
  cat > "$plan/minifuze2.service" <<EOF
[Unit]
Description=MiniFuse 2 audio mixer and controls
After=pipewire.service
Requires=pipewire.service
PartOf=pipewire.service

[Service]
Type=simple
UMask=0077
ExecStartPre=/usr/bin/mkdir -p "%S/minifuze2"
ExecStart="$base/bin/minifuze2-engine" panel --prefix minifuze2 --state "%S/minifuze2/mixer.json" --session-state "%S/minifuze2/working.json" --ready-file "%S/minifuze2/ready.json" --http-port 39713 --ctl /dev/minifuze2ctl
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
EOF
fi
put_file() {
  local source=$1 target=$2 mode=$3
  mkdir -p -- "$(dirname -- "$target")"
  [[ ! -e $target && ! -L $target ]] || { echo "Target appeared during install: $target" >&2; return 1; }
  # Exclusive creation avoids overwriting an existing file after preflight.
  (set -o noclobber; : > "$target")
  created+=("$target")
  cat -- "$source" > "$target"
  chmod "$mode" "$target"
}
put_file "$repo/build/minifuze2" "${targets[0]}" 755
put_file "$repo/build/minifuze2-engine" "${targets[1]}" 755
put_file "$repo/packaging/io.github.minifuze2.Control.svg" "${targets[2]}" 644
put_file "$plan/io.github.minifuze2.Control.desktop" "${targets[3]}" 644
if ! $reuse; then
  put_file "$plan/minifuze2.service" "$unit" 644
  systemctl --user daemon-reload
  enable_attempted=true
  systemctl --user enable minifuze2.service
fi
printf '%s\n' "${targets[@]}" > "$plan/owned-files"
sha256sum -- "${targets[@]}" > "$plan/installed.sha256"
put_file "$plan/owned-files" "$owned" 600
put_file "$plan/installed.sha256" "$manifest" 600
committed=true
refresh_menu
echo 'Installed. Open MiniFuse 2 for Linux from the application menu.'
echo 'Closing the window keeps the audio service running.'
