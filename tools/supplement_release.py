#!/usr/bin/env python3
"""Supplement the existing 0.1.0 DRAFT; preserve the tagged archives and tag."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

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


def download_hashes(names, verify_sidecars=False):
    with tempfile.TemporaryDirectory(prefix='minifuze-release-audit-') as temporary:
        command = ['gh', 'release', 'download', TAG, '--dir', temporary]
        for name in sorted(names):
            command += ['--pattern', name]
        subprocess.run(command, check=True)
        files = {p.name: p for p in Path(temporary).iterdir() if p.is_file()}
        if files.keys() != names:
            raise ValueError('Incomplete release download')
        hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}
        if verify_sidecars:
            for name in names:
                if not name.endswith('.sha256'):
                    sidecar = files.get(name+'.sha256')
                    if not sidecar or sidecar.read_text() != hashes[name]+'  '+name+'\n':
                        raise ValueError('Invalid downloaded checksum: '+name)
            subprocess.run([sys.executable, str(Path(__file__).with_name('release.py')),
                            'verify', '--version', '0.1.0',
                            str(files['minifuze2-0.1.0-source.tar.gz']),
                            str(files['minifuze2-0.1.0-ubuntu-24.04-x86_64.tar.gz'])], check=True)
        return hashes


def review():
    """Verify existing downloads and refresh notes without replacing any asset."""
    revision = subprocess.check_output(['git', 'rev-parse', TAG+'^{commit}'], text=True).strip()
    if revision != SOURCE:
        raise ValueError('The original application tag must remain unchanged')
    expected = ORIGINAL | NATIVE | {name+'.sha256' for name in NATIVE}
    before = draft()
    assets_before = {asset['name']: asset for asset in before['assets']}
    if assets_before.keys() != expected:
        raise ValueError('Unexpected final release asset set')
    hashes_before = download_hashes(expected, verify_sidecars=True)
    draft()
    notes = Path('docs/validation/release-notes-0.1.0.md')
    if notes.read_text().splitlines()[0] != '# MiniFuse 2 for Linux 0.1.0':
        raise ValueError('Release note version mismatch')
    subprocess.run(['gh', 'release', 'edit', TAG, '--notes-file', str(notes)], check=True)
    after = draft()
    assets_after = {asset['name']: asset for asset in after['assets']}
    if assets_after.keys() != expected:
        raise ValueError('Final release asset set changed')
    for name in expected:
        if any(assets_before[name].get(key) != assets_after[name].get(key)
               for key in ('id', 'apiUrl', 'size', 'createdAt', 'digest')):
            raise ValueError('Final review changed asset identity: '+name)
    if hashes_before != download_hashes(expected, verify_sidecars=True):
        raise ValueError('Final review changed release bytes')
    body = json.loads(subprocess.check_output([
        'gh', 'release', 'view', TAG, '--json', 'body'], text=True))['body']
    if body.rstrip() != notes.read_text().rstrip():
        raise ValueError('Final draft notes do not match the reviewed file')
    after = draft()
    print('::notice title=Final release check passed::'+after['url'])
    print('::notice::All 18 existing assets, 9 SHA256 sidecars, original archive manifests and final notes verified. No assets replaced; stable release remains a draft.')


def main():
    if sys.argv[1:] == ['--review']:
        review()
        return
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
    original_hashes = download_hashes(ORIGINAL)
    for name in sorted(files):
        draft()  # Recheck the publication boundary before each mutation.
        subprocess.run(['gh', 'release', 'upload', TAG, str(files[name]), '--clobber'], check=True)
    draft()
    subprocess.run(['gh', 'release', 'edit', TAG, '--notes-file', 'docs/validation/release-notes-0.1.0.md'], check=True)
    after = draft()
    assets = {asset['name']: asset for asset in after['assets']}
    if assets.keys() != ORIGINAL | expected:
        raise ValueError('Incomplete release asset set')
    for name, asset in originals.items():
        # Editing draft notes can change browser URLs. Downloads also increase
        # counters. Require the original asset identity and verify its bytes.
        if any(assets[name].get(key) != asset.get(key)
               for key in ('id', 'apiUrl', 'size', 'createdAt', 'digest')):
            raise ValueError('An original archive changed: '+name)
    remote_hashes = download_hashes(ORIGINAL | expected)
    if any(remote_hashes[name] != digest for name, digest in original_hashes.items()):
        raise ValueError('An original archive changed bytes')
    if any(remote_hashes[name] != hashlib.sha256(path.read_bytes()).hexdigest()
           for name, path in files.items()):
        raise ValueError('Uploaded native assets differ from the checked build')
    after = draft()
    print('::notice title=Native packages added to stable draft::'+after['url'])
    print('::notice::Downloaded and verified all 18 assets: 7 native packages/source bundles, 7 SHA256 sidecars and 4 unchanged original assets. Draft remains unpublished.')


if __name__ == '__main__':
    main()
