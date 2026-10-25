#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Create local reviewable archives; never uploads or publishes anything.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=false
if (($#)); then
  [[ $# == 1 && $1 == --binary ]] || { echo 'Usage: package_release.sh [--binary]' >&2; exit 2; }
  binary=true
fi
version=0.1.0-preview
name="minifuze2-$version"
suffix=source
if $binary; then
  suffix="linux-$(uname -m)"
  [[ -x $repo/build/minifuze2 && -x $repo/build/minifuze2-engine ]] || { echo 'Build first: make' >&2; exit 1; }
  command -v strip >/dev/null
fi
mkdir -p -- "$repo/dist"
archive="$repo/dist/$name-$suffix.tar.gz"
[[ ! -e $archive && ! -e $archive.sha256 ]] || { echo 'Archive already exists; retain it or change the version' >&2; exit 1; }
stage=$(mktemp -d "$repo/dist/.release.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
bundle="$stage/$name"
mkdir -- "$bundle"
copy_file() { mkdir -p -- "$bundle/$(dirname -- "$1")"; cp -- "$repo/$1" "$bundle/$1"; }
for file in Makefile README.md README.ru.md CONTRIBUTING.md CHANGELOG.md TODO.md NOTICE.md LICENSE .gitignore .gitattributes; do copy_file "$file"; done
for file in Makefile dkms.conf minifuze2_ctl.c minifuze2_selection.h minifuze2_uapi.h; do copy_file "kernel/$file"; done
while IFS= read -r -d '' file; do
  relative=${file#"$repo/"}
  case "$relative" in
    */__pycache__/*) continue ;;
    *.c|*.h|*.css|*.js|*.html|*.sh|*.py|*.md|*.json|*.mfp|*.svg|*.txt|*.yml|*.yaml|*.png) copy_file "$relative" ;;
  esac
done < <(find "$repo/native" "$repo/web" "$repo/tools" "$repo/tests" "$repo/docs" "$repo/packaging" "$repo/LICENSES" "$repo/.github" -type f -print0)
if $binary; then
  mkdir -- "$bundle/build"
  cp -- "$repo/build/minifuze2" "$repo/build/minifuze2-engine" "$bundle/build/"
  strip --strip-debug "$bundle/build/minifuze2" "$bundle/build/minifuze2-engine"
fi
(cd -- "$bundle" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
tar --sort=name --owner=0 --group=0 --numeric-owner -C "$stage" -czf "$archive.tmp" "$name"
mv -- "$archive.tmp" "$archive"
(cd -- "$repo/dist" && sha256sum "$(basename -- "$archive")" > "$(basename -- "$archive").sha256")
printf 'Created %s\n' "$archive"
