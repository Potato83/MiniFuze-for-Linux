#!/usr/bin/env python3
"""Supplement the existing 0.1.0 DRAFT; preserve the tagged archives and tag."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

TAG = 'v0.1.0'
SOURCE = '676bec2bccc08bfa7ef6f645e5d09cfc6f1b3539'
ORIGINAL = {'minifuze2-0.1.0-'+kind+'.tar.gz'+suffix
            for kind in ('source', 'ubuntu-24.04-x86_64') for suffix in ('', '.sha256')}
NATIVE = {
    'minifuze2-0.1.0-1-arch-linux-x86_64.pkg.tar.zst',
    'minifuze2-0.1.0-1-cachyos-x86_64.pkg.tar.zst',
    'minifuze2-ctl-dkms-0.2.0-1-any.pkg.tar.zst',
    'minifuze2-0.1.0-1.src.tar.gz',
    'minifuze2-ctl-dkms-0.2.0-1.src.tar.gz',
    'minifuze2_0.1.0-1_amd64.deb',
    'minifuze2-ctl-dkms_0.2.0-1_all.deb',
}


def draft():
    release = json.loads(subprocess.check_output([
        'gh', 'release', 'view', TAG, '--json', 'tagName,isDraft,isPrerelease,assets,url'], text=True))
    if release['tagName'] != TAG or not release['isDraft'] or release['isPrerelease']:
        raise ValueError('Only the existing stable draft may be supplemented')
    return release


def main():
    directory = Path(sys.argv[1])
    files = {p.name: p for p in directory.iterdir() if p.is_file()}
    expected = NATIVE | {name+'.sha256' for name in NATIVE}
    if files.keys() != expected:
        raise ValueError('Unexpected native asset set: '+str(files.keys() ^ expected))
    for name in NATIVE:
        digest = hashlib.sha256(files[name].read_bytes()).hexdigest()
        if files[name+'.sha256'].read_text() != digest+'  '+name+'\n':
            raise ValueError('Invalid checksum: '+name)
    revision = subprocess.check_output(['git', 'rev-parse', TAG+'^{commit}'], text=True).strip()
    if revision != SOURCE:
        raise ValueError('The original application tag must remain unchanged')
    before = draft()
    originals = {asset['name']: asset for asset in before['assets'] if asset['name'] in ORIGINAL}
    if originals.keys() != ORIGINAL:
        raise ValueError('Original release archives must already exist')
    unknown = {asset['name'] for asset in before['assets']} - ORIGINAL - expected
    if unknown:
        raise ValueError('Review unexpected existing assets before upload: '+str(unknown))
    for name in sorted(files):
        draft()  # Recheck the publication boundary before each mutation.
        subprocess.run(['gh', 'release', 'upload', TAG, str(files[name]), '--clobber'], check=True)
    draft()
    subprocess.run(['gh', 'release', 'edit', TAG, '--notes-file', 'docs/release-notes.md'], check=True)
    after = draft()
    assets = {asset['name']: asset for asset in after['assets']}
    if assets.keys() != ORIGINAL | expected:
        raise ValueError('Incomplete release asset set')
    for name, asset in originals.items():
        if assets[name] != asset:
            raise ValueError('An original archive changed: '+name)
    print('::notice title=Native packages added to stable draft::'+after['url'])
    print('::notice::Verified 7 native packages/source bundles and 7 SHA256 sidecars; original 4 assets retained; draft remains unpublished.')


if __name__ == '__main__':
    main()
