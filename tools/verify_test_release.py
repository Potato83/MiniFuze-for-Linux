#!/usr/bin/env python3
"""Verify the complete 0.1.1 artifact set and all published SHA256 sidecars."""
import hashlib
from pathlib import Path
import sys

NAMES = {
    'minifuze2-0.1.1-source.tar.gz',
    'minifuze2-0.1.1-ubuntu-24.04-x86_64.tar.gz',
    'minifuze2-0.1.1-1-arch-linux-x86_64.pkg.tar.zst',
    'minifuze2-0.1.1-1-cachyos-x86_64.pkg.tar.zst',
    'minifuze2-ctl-dkms-0.2.0-1-any.pkg.tar.zst',
    'minifuze2-0.1.1-1.src.tar.gz',
    'minifuze2-ctl-dkms-0.2.0-1.src.tar.gz',
    'minifuze2_0.1.1-1_amd64.deb',
    'minifuze2-ctl-dkms_0.2.0-1_all.deb',
}


def verify(directories):
    files = {}
    for directory in directories:
        for path in Path(directory).iterdir():
            if not path.is_file() or path.name in files:
                raise ValueError('Unexpected or duplicate artifact: '+str(path))
            files[path.name] = path
    expected = NAMES | {name+'.sha256' for name in NAMES}
    if files.keys() != expected:
        raise ValueError('Unexpected artifact inventory: '+str(files.keys() ^ expected))
    for name in NAMES:
        checksum = hashlib.sha256(files[name].read_bytes()).hexdigest()
        if files[name+'.sha256'].read_text() != checksum+'  '+name+'\n':
            raise ValueError('Invalid checksum for '+name)
    print(f'Verified {len(NAMES)} artifacts and their checksums')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit('Provide one or more artifact directories')
    verify(sys.argv[1:])
