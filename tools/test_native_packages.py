#!/usr/bin/env python3
"""Inspect real package payloads; exercise pacman transactions in a private root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = json.loads((ROOT/'packaging/native/upstream.json').read_text())

def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def inspect(root, component, payload_only=True):
    files = [p for p in root.rglob('*') if p.is_file()]
    assert files
    assert not any(p.is_symlink() for p in root.rglob('*'))
    if payload_only:
        assert not any(p.relative_to(root).parts[0] in ('etc', 'home', 'var') for p in files)
    assert not any(p.suffix == '.ko' for p in files)
    name = 'minifuze2' if component == 'app' else 'minifuze2-ctl-dkms'
    metadata = json.loads((root/f'usr/share/doc/{name}/BUILD-INFO.json').read_text())
    assert metadata['source_revision'] == UPSTREAM['source_revision']
    assert metadata['application_version'] == UPSTREAM['application_version']
    if component == 'app':
        for binary in ('minifuze2', 'minifuze2-engine'):
            version = subprocess.check_output([str(root/'usr/bin'/binary), '--version'], text=True)
            assert version.strip().split()[-1] == UPSTREAM['application_version']
        run('desktop-file-validate', root/'usr/share/applications/io.github.minifuze2.Control.desktop')
        unit = (root/'usr/lib/systemd/user/minifuze2.service').read_text()
        assert '/usr/bin/minifuze2-engine' in unit and '%S/minifuze2/working.json' in unit
        assert '/home/' not in unit
    else:
        run('bash', '-n', root/'usr/bin/minifuze2-ctl-setup')
        assert (root/'usr/src/minifuze2-ctl-0.2.0/dkms.conf').is_file()
        assert 'KDIR=/lib/modules/${kernelver}/build' in (root/'usr/src/minifuze2-ctl-0.2.0/dkms.conf').read_text()
        assert not (root/'usr/lib/modules-load.d').exists()
        assert (root/'usr/lib/sysusers.d/minifuze2ctl.conf').is_file()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('format', choices=('arch', 'deb'))
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    extension = '*.pkg.tar.zst' if args.format == 'arch' else '*.deb'
    packages = sorted(args.directory.resolve().glob(extension))
    assert packages, 'No native packages found'
    for package in packages:
        expected = package.with_name(package.name+'.sha256').read_text()
        assert expected == hashlib.sha256(package.read_bytes()).hexdigest()+'  '+package.name+'\n'
        with tempfile.TemporaryDirectory(prefix='minifuze-native-check-') as temporary:
            work = Path(temporary)
            payload = work/'payload'
            payload.mkdir()
            component = 'dkms' if package.name.startswith('minifuze2-ctl-dkms') else 'app'
            if args.format == 'arch':
                run('bsdtar', '-xf', package, '-C', payload)
                pkginfo = (payload/'.PKGINFO').read_text()
                if component == 'app':
                    assert 'depend = pipewire-audio\n' in pkginfo
                    assert 'depend = libssl.so=' in pkginfo and 'depend = libcrypto.so=' in pkginfo
            else:
                run('dpkg-deb', '--extract', package, payload)
                run('dpkg-deb', '--control', package, work/'control')
                control = (work/'control/control').read_text()
                if component == 'app':
                    assert 'pipewire-bin' in control and 'libgtk-4-1 (>= 4.12)' in control
                    assert not (work/'control/postinst').exists()
                else:
                    for script in ('postinst', 'prerm'):
                        run('sh', '-n', work/'control'/script)
            inspect(payload, component)
            if args.format == 'arch':
                # No host package database, hooks, dependencies or user service is touched.
                root = work/'root'
                (root/'var/lib/pacman').mkdir(parents=True)
                (work/'cache').mkdir()
                (work/'pacman.conf').write_text('[options]\nArchitecture = auto\nLocalFileSigLevel = Never\n')
                pacman = ['pacman', '--config', work/'pacman.conf', '--root', root,
                          '--dbpath', root/'var/lib/pacman', '--cachedir', work/'cache',
                          '--logfile', work/'pacman.log', '--nodeps', '--nodeps', '--noscriptlet', '--noconfirm']
                state = root/'home/desktop/.local/state/minifuze2/mixer.json'
                state.parent.mkdir(parents=True)
                state.write_text('retained user preset\n')
                name = 'minifuze2' if component == 'app' else 'minifuze2-ctl-dkms'
                for _ in range(2):
                    run(*pacman, '-U', package)
                    inspect(root, component, payload_only=False)
                run(*pacman, '-R', name)
                assert state.read_text() == 'retained user preset\n'
                assert not (root/'usr/bin'/('minifuze2' if component == 'app' else 'minifuze2-ctl-setup')).exists()
            print('Verified '+package.name, flush=True)


if __name__ == '__main__':
    main()
