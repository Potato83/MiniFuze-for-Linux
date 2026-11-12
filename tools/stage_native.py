#!/usr/bin/env python3
"""Stage native package payloads; never install on the build host."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def stage(source, assets, dest, component, metadata):
    source, assets, dest = map(Path, (source, assets, dest))
    if dest.exists() and any(dest.iterdir()):
        raise ValueError('Staging directory must be empty')
    dest.mkdir(parents=True, exist_ok=True)

    def put(original, relative, mode=0o644):
        target = dest/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
        target.chmod(mode)

    if component == 'app':
        for name in ('minifuze2', 'minifuze2-engine'):
            binary = source/'build'/name
            version = subprocess.check_output([str(binary), '--version'], text=True).strip()
            if version.split()[-1] != metadata['application_version']:
                raise ValueError('Stale application binary: '+name)
            put(binary, 'usr/bin/'+name, 0o755)
        put(assets/'minifuze2.service', 'usr/lib/systemd/user/minifuze2.service')
        put(assets/'io.github.minifuze2.Control.desktop',
            'usr/share/applications/io.github.minifuze2.Control.desktop')
        put(source/'packaging/io.github.minifuze2.Control.svg',
            'usr/share/icons/hicolor/scalable/apps/io.github.minifuze2.Control.svg')
        license_names = ('LICENSE', 'NOTICE.md')
        doc_name = 'minifuze2'
    elif component == 'dkms':
        version = metadata['control_module_version']
        config = (source/'kernel/dkms.conf').read_text()
        if f'PACKAGE_VERSION="{version}"' not in config:
            raise ValueError('Control module version mismatch')
        for name in ('Makefile', 'dkms.conf', 'minifuze2_ctl.c',
                     'minifuze2_selection.h', 'minifuze2_uapi.h'):
            put(source/'kernel'/name, f'usr/src/minifuze2-ctl-{version}/'+name)
        # Older DKMS defaults call `make` in the source tree. Select its target
        # headers explicitly instead of accidentally building for uname -r.
        dkms_config = dest/f'usr/src/minifuze2-ctl-{version}/dkms.conf'
        with dkms_config.open('a') as handle:
            handle.write('\nMAKE[0]="make KDIR=/lib/modules/${kernelver}/build"\n')
        put(assets/'70-minifuze2ctl.rules', 'usr/lib/udev/rules.d/70-minifuze2ctl.rules')
        put(assets/'minifuze2ctl.conf', 'usr/lib/sysusers.d/minifuze2ctl.conf')
        put(assets/'minifuze2-ctl-setup', 'usr/bin/minifuze2-ctl-setup', 0o755)
        put(assets/'README.control', 'usr/share/doc/minifuze2-ctl-dkms/README')
        license_names = ('LICENSE', 'NOTICE.md', 'LICENSES/GPL-2.0-only.txt',
                         'LICENSES/Linux-syscall-note.txt')
        doc_name = 'minifuze2-ctl-dkms'
    else:
        raise ValueError('Unknown component')
    for name in license_names:
        put(source/name, f'usr/share/licenses/{doc_name}/'+Path(name).name)
    info = dest/f'usr/share/doc/{doc_name}/BUILD-INFO.json'
    info.parent.mkdir(parents=True, exist_ok=True)
    info.write_text(json.dumps(metadata, indent=2, sort_keys=True)+'\n')
    # No prebuilt modules, user homes, personal configuration or autoload policy.
    for file in dest.rglob('*'):
        if file.is_symlink() or (file.is_file() and file.suffix == '.ko'):
            raise ValueError('Unexpected staged file: '+str(file))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'assets', 'dest', 'metadata'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--component', choices=('app', 'dkms'), required=True)
    args = parser.parse_args()
    stage(args.source, args.assets, args.dest, args.component,
          json.loads(args.metadata.read_text()))
