#!/usr/bin/env python3
"""One-time cleanup of exactly the reviewed v0.1.1 assets; default is read-only."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPO = 'Potato83/MiniFuze-for-Linux'
TAG = 'v0.1.1'
TAG_COMMIT = 'b0241e17f8c6889af1166a4e07733fca017e62e1'
KEEP = {
    'minifuze2-0.1.1-1-arch-linux-x86_64.pkg.tar.zst',
    'minifuze2-0.1.1-1-cachyos-x86_64.pkg.tar.zst',
    'minifuze2_0.1.1-1_amd64.deb',
    'minifuze2-ctl-dkms-0.2.0-1-any.pkg.tar.zst',
    'minifuze2-ctl-dkms_0.2.0-1_all.deb',
}


def release():
    request = urllib.request.Request(
        f'https://api.github.com/repos/{REPO}/releases/tags/{TAG}',
        headers={'User-Agent': 'minifuze-release-cleanup'})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def inventory(data):
    return {a['name']: {'id': a['id'], 'digest': a['digest']} for a in data['assets']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((ROOT/'tools/cleanup-release-0.1.1.json').read_text())
    if len(manifest) != 18 or not KEEP < manifest.keys():
        raise ValueError('Unexpected reviewed release manifest')
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', TAG+'^{commit}'], text=True).strip()
    if commit != TAG_COMMIT:
        raise ValueError('Unexpected tag commit')
    before = release()
    if before['tag_name'] != TAG or before['draft'] or not before['prerelease'] or inventory(before) != manifest:
        raise ValueError('Release flags or assets changed since backup; refusing to modify')
    notes = ROOT/'docs/release-notes.md'
    for name in KEEP:
        digest = manifest[name]['digest']
        if not digest.startswith('sha256:') or digest[7:]+'  '+name not in notes.read_text():
            raise ValueError('Missing package checksum in release notes: '+name)
    remove = sorted(manifest.keys() - KEEP)
    print(f'Plan: retain {len(KEEP)} packages; remove {len(remove)} other assets.', flush=True)
    if not args.execute:
        return
    subprocess.run(['gh','release','edit',TAG,'--repo',REPO,'--notes-file',str(notes)], check=True)
    for name in remove:
        subprocess.run(['gh','api','--method','DELETE',f'repos/{REPO}/releases/assets/{manifest[name]["id"]}'], check=True)
        print('Removed '+name, flush=True)
    after = release()
    if after['draft'] or not after['prerelease'] or inventory(after) != {k: manifest[k] for k in KEEP}:
        raise ValueError('Final release state differs from the reviewed five packages')
    if after['body'].rstrip() != notes.read_text().rstrip():
        raise ValueError('Release notes differ from the reviewed version')
    for asset in after['assets']:
        request = urllib.request.Request(asset['browser_download_url'], headers={'User-Agent':'minifuze-release-cleanup'})
        with urllib.request.urlopen(request, timeout=120) as response:
            digest = hashlib.sha256(response.read()).hexdigest()
        if 'sha256:'+digest != manifest[asset['name']]['digest']:
            raise ValueError('Downloaded package changed: '+asset['name'])
    print('Release cleanup verified: all five original packages intact.', flush=True)


if __name__ == '__main__':
    main()
