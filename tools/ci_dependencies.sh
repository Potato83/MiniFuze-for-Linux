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
log=$(mktemp)
trap 'rm -f -- "$log"' EXIT
apt_step() {
  if sudo apt-get -o Acquire::Retries=2 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 "$@" 2>&1 | tee "$log"; then
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
apt_step update
apt_step install -y "${packages[@]}"
