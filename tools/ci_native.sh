#!/usr/bin/env bash
# Keep failure evidence visible in public check annotations, not only job logs.
set -euo pipefail
native_log=$(mktemp)
trap 'rm -f -- "$native_log"' EXIT
if "$@" 2>&1 | tee "$native_log"; then
    exit 0
else
    native_result=$?
fi
python3 - "$native_log" <<'PY'
from pathlib import Path
import sys
tail = '\n'.join(Path(sys.argv[1]).read_text(errors='replace').splitlines()[-40:])[-7000:]
escaped = tail.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
print('::error title=Native package verification failed::'+escaped)
PY
exit "$native_result"
