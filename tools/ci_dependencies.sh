#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Keep hosted-runner package failures bounded and visible without private logs.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo 'This helper is for GitHub Actions runners only.' >&2; exit 2; }
case ${1:-} in
  app) packages=(build-essential pkg-config libgtk-4-dev libsoup-3.0-dev libjson-c-dev libmicrohttpd-dev libssl-dev pipewire-bin wireplumber xvfb fonts-dejavu-core desktop-file-utils) ;;
  kernel) packages=(build-essential linux-headers-generic) ;;
  *) echo 'Usage: ci_dependencies.sh app|kernel' >&2; exit 2 ;;
esac
# Select a reachable Ubuntu mirror before apt's slower package retries. APT
# still verifies Ubuntu's signed Release metadata and every package hash.
mirror=''
for candidate in https://archive.ubuntu.com/ubuntu https://mirrors.edge.kernel.org/ubuntu https://mirrors.mit.edu/ubuntu https://mirrors.xtom.com/ubuntu; do
  if curl -4 --fail --silent --show-error --location --max-time 8 "$candidate/dists/noble/InRelease" -o /tmp/minifuze-ci-inrelease 2>/tmp/minifuze-ci-mirror-error; then
    if head -n 1 /tmp/minifuze-ci-inrelease | grep -q 'BEGIN PGP SIGNED MESSAGE'; then
      mirror=$candidate
      break
    fi
  fi
  printf '::notice::Ubuntu mirror unavailable: %s\n' "$candidate"
done
if [[ -z $mirror ]]; then
  printf '::error::No Ubuntu mirror reachable over HTTPS; runner DNS or outbound networking is unavailable.\n'
  exit 1
fi
printf '::notice::Selected Ubuntu mirror: %s\n' "$mirror"
if [[ -f /etc/apt/apt-mirrors.txt ]]; then
  printf '%s/\n' "$mirror" | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
fi
if [[ -f /etc/apt/apt-security-mirrors.txt ]]; then
  printf '%s/\n' "$mirror" | sudo tee /etc/apt/apt-security-mirrors.txt >/dev/null
fi
log=$(mktemp)
trap 'rm -f -- "$log"' EXIT
apt_step() {
  if timeout --signal=TERM --kill-after=10s 180s sudo apt-get -o Acquire::ForceIPv4=true -o Acquire::Retries=2 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 "$@" 2>&1 | tee "$log"; then
    return
  fi
  {
    printf '### Package installation failed\n\n```text\n'
    tail -n 45 "$log"
    printf '\n```\n'
  } >> "${GITHUB_STEP_SUMMARY:?}"
  # Check annotations are public even when access to the full job log needs
  # authentication. Escape workflow-command syntax before emitting the tail.
  local details
  details=$(tail -n 18 "$log")
  details=${details//'%'/'%25'}
  details=${details//$'\r'/'%0D'}
  details=${details//$'\n'/'%0A'}
  printf '::error::APT failed: %s\n' "$details"
  return 1
}
apt_step -o APT::Update::Error-Mode=any update
apt_step install -y "${packages[@]}"
