#!/usr/bin/env python3
"""Build native packages from a checksum-pinned release, without installing them."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT/'packaging/native'
UPSTREAM = json.loads((ASSETS/'upstream.json').read_text())


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(platform):
    archive_info = ROOT/'BUILD-INFO.json'
    if archive_info.is_file():
        revision = json.loads(archive_info.read_text())['source_revision']
        dirty = True  # An extraction can be edited; do not certify a clean checkout.
    else:
        revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        dirty = bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain']))
    return dict(UPSTREAM, packaging_revision=revision, packaging_dirty=dirty, platform=platform)


def source_archive():
    cache = Path(os.environ.get('XDG_CACHE_HOME', Path.home()/'.cache'))/'minifuze-native-packages'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache/f'minifuze2-{UPSTREAM["application_version"]}-upstream.tar.gz'
    if not archive.exists():
        with tempfile.NamedTemporaryFile(dir=cache, delete=False) as handle:
            pending = Path(handle.name)
        try:
            with urllib.request.urlopen(UPSTREAM['source_url'], timeout=90) as response:
                with pending.open('wb') as handle:
                    shutil.copyfileobj(response, handle)
            if digest(pending) != UPSTREAM['source_sha256']:
                raise ValueError('Downloaded source checksum mismatch')
            pending.replace(archive)
        finally:
            pending.unlink(missing_ok=True)
    if digest(archive) != UPSTREAM['source_sha256']:
        raise ValueError('Cached source checksum mismatch; remove the damaged cache file')
    with tarfile.open(archive, 'r:gz') as tar:
        os.environ.setdefault('SOURCE_DATE_EPOCH', str(tar.getmembers()[0].mtime))
    return archive


def extract_source(archive, dest):
    prefix = 'MiniFuze-for-Linux-'+UPSTREAM['source_revision']
    with tarfile.open(archive, 'r:gz') as tar:
        for member in tar.getmembers():
            parts = Path(member.name).parts
            if not parts or parts[0] != prefix or '..' in parts or not (member.isdir() or member.isfile()):
                raise ValueError('Unsafe source archive member')
        tar.extractall(dest, filter='data')
    source = dest/prefix
    if (source/'VERSION').read_text().strip() != UPSTREAM['application_version']:
        raise ValueError('Pinned source version mismatch')
    return source


def export_arch(dest, platform):
    """Self-contained AUR recipes; remote source is pinned by commit AND hash."""
    info = metadata(platform)
    recipes = []
    for component, pkgname in (('app', 'minifuze2'), ('dkms', 'minifuze2-ctl-dkms')):
        folder = dest/pkgname
        folder.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(ROOT/'tools/stage_native.py', folder/'stage_native.py')
        for asset in ASSETS.iterdir():
            if asset.is_file() and asset.name != 'upstream.json':
                shutil.copyfile(asset, folder/asset.name)
        (folder/'native-build.json').write_text(json.dumps(info, indent=2, sort_keys=True)+'\n')
        names = sorted(p.name for p in folder.iterdir())
        sources = [f"'upstream.tar.gz::{UPSTREAM['source_url']}'"]+[repr(name) for name in names]
        hashes = [UPSTREAM['source_sha256']]+[digest(folder/name) for name in names]
        values = {
            'VERSION': UPSTREAM['application_version' if component == 'app' else 'control_module_version'],
            'RELEASE': str(UPSTREAM['package_release']), 'REVISION': UPSTREAM['source_revision'],
            'SOURCES': '\n        '.join(sources), 'HASHES': '\n            '.join(map(repr, hashes)),
        }
        recipe = (ROOT/f'packaging/arch/PKGBUILD.{component}.in').read_text()
        for key, value in values.items():
            recipe = recipe.replace('@'+key+'@', value)
        (folder/'PKGBUILD').write_text(recipe)
        recipes.append(folder)
    return recipes


def copy_artifact(source, output, name=None):
    target = output/(name or source.name)
    if target.exists() or target.with_name(target.name+'.sha256').exists():
        raise ValueError('Refusing to replace existing artifact: '+target.name)
    shutil.copyfile(source, target)
    target.with_name(target.name+'.sha256').write_text(digest(target)+'  '+target.name+'\n')
    print('Created '+target.name, flush=True)


def build_arch(output, platform):
    archive = source_archive()
    with tempfile.TemporaryDirectory(prefix='minifuze-native-') as temporary:
        work = Path(temporary)
        config = work/'makepkg.conf'
        config.write_text(Path('/etc/makepkg.conf').read_text()+'''
# Portable CPU target and public build identity for release packages.
CFLAGS="-O2 -pipe -march=x86-64 -mtune=generic"
CXXFLAGS="$CFLAGS"
PACKAGER="MiniFuse for Linux contributors <noreply@github.com>"
PKGEXT='.pkg.tar.zst'
SRCEXT='.src.tar.gz'
''')
        for recipe in export_arch(work, platform):
            shutil.copyfile(archive, recipe/'upstream.tar.gz')
            with (recipe/'.SRCINFO').open('w') as handle:
                run('makepkg', '--config', config, '--printsrcinfo', cwd=recipe, stdout=handle)
            run('makepkg', '--config', config, '--cleanbuild', '--noconfirm', cwd=recipe)
            binaries = list(recipe.glob('*.pkg.tar.zst'))
            if len(binaries) != 1:
                raise ValueError('Expected exactly one package')
            package = binaries[0]
            name = package.name.replace('-x86_64.pkg.tar.zst', f'-{platform}-x86_64.pkg.tar.zst')
            # DKMS is architecture independent. Produce it only in the Arch job.
            if platform == 'arch-linux' or recipe.name == 'minifuze2':
                copy_artifact(package, output, name)
            if platform == 'arch-linux':
                run('makepkg', '--config', config, '--source', '--noconfirm', cwd=recipe)
                bundles = list(recipe.glob('*.src.tar.gz'))
                if len(bundles) != 1:
                    raise ValueError('Expected one AUR source bundle')
                copy_artifact(bundles[0], output)


def deb_control(name, version, arch, depends, description):
    return f'''Package: {name}
Version: {version}-{UPSTREAM['package_release']}
Section: sound
Priority: optional
Architecture: {arch}
Maintainer: MiniFuse for Linux contributors <noreply@github.com>
Homepage: https://github.com/Potato83/MiniFuze-for-Linux
Depends: {depends}
Description: {description}
 Independent MiniFuse 2 mixer and optional bounded hardware controls for Linux.
'''


def build_deb(output):
    if not shutil.which('dpkg-deb') or not shutil.which('dpkg-shlibdeps'):
        raise ValueError('Build .deb packages on Ubuntu 24.04 with dpkg-dev installed')
    spec = importlib.util.spec_from_file_location('stage_native', ROOT/'tools/stage_native.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    info = metadata('ubuntu-24.04-amd64')
    with tempfile.TemporaryDirectory(prefix='minifuze-deb-') as temporary:
        work = Path(temporary)
        source = extract_source(source_archive(), work)
        run('make', '-j2', 'CFLAGS=-O2 -pipe -march=x86-64 -mtune=generic', cwd=source)
        run('make', 'check', cwd=source)
        # dpkg-shlibdeps reads the actual ELF dependencies, including t64 names.
        (source/'debian').mkdir()
        (source/'debian/control').write_text('Source: minifuze2\n\nPackage: minifuze2\nArchitecture: any\nDescription: MiniFuse mixer\n')
        shlibs = subprocess.check_output([
            'dpkg-shlibdeps', '-O', '-e'+str(source/'build/minifuze2'),
            '-e'+str(source/'build/minifuze2-engine')], cwd=source, text=True).strip()
        if not shlibs.startswith('shlibs:Depends=') or '\n' in shlibs:
            raise ValueError('Unexpected dpkg-shlibdeps output')
        for component, name in (('app', 'minifuze2'), ('dkms', 'minifuze2-ctl-dkms')):
            dest = work/name
            module.stage(source, ASSETS, dest, component, info)
            control = dest/'DEBIAN'
            control.mkdir()
            if component == 'app':
                arch, version = 'amd64', UPSTREAM['application_version']
                depends = shlibs.removeprefix('shlibs:Depends=')+', libgtk-4-1 (>= 4.12), pipewire, pipewire-bin, wireplumber, systemd'
                description = 'MiniFuse 2 GTK mixer and control panel'
                for executable in (dest/'usr/bin').iterdir():
                    run('strip', '--strip-unneeded', executable)
            else:
                arch, version = 'all', UPSTREAM['control_module_version']
                depends = 'dkms (>= 2.8), kmod, udev, systemd, passwd, bash'
                description = 'MiniFuse 2 optional control endpoint (DKMS sources)'
                for script in ('postinst', 'prerm'):
                    target = control/script
                    target.write_text((ROOT/f'packaging/debian/dkms.{script}').read_text().replace('@VERSION@', version))
                    target.chmod(0o755)
            (control/'control').write_text(deb_control(name, version, arch, depends, description))
            # Debian policy expects the license under /usr/share/doc/PACKAGE.
            license_file = 'LICENSE' if component == 'app' else 'GPL-2.0-only.txt'
            shutil.copyfile(dest/f'usr/share/licenses/{name}/{license_file}', dest/f'usr/share/doc/{name}/copyright')
            artifact = work/f'{name}_{version}-{UPSTREAM["package_release"]}_{arch}.deb'
            run('dpkg-deb', '--root-owner-group', '--build', dest, artifact)
            copy_artifact(artifact, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('format', choices=('arch', 'deb', 'arch-recipes'))
    parser.add_argument('--platform', choices=('arch-linux', 'cachyos'), default='arch-linux')
    parser.add_argument('--output-dir', type=Path, default=ROOT/'dist/native')
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.format == 'arch-recipes':
        export_arch(args.output_dir, args.platform)
    elif args.format == 'arch':
        build_arch(args.output_dir, args.platform)
    else:
        build_deb(args.output_dir)


if __name__ == '__main__':
    main()
