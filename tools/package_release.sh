#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Developer tooling only; the installed application does not require Python.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec python3 "$repo/tools/release.py" package "$@"
