#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create, verify and classify release files. Standard library; never publishes."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VERSION_RE = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?')
ROOT_FILES = {'VERSION', 'Makefile', 'README.md', 'README.ru.md', 'CONTRIBUTING.md',
              'CHANGELOG.md', 'TODO.md', 'NOTICE.md', 'LICENSE', '.gitignore', '.gitattributes'}
SOURCE_DIRS = {'native', 'web', 'tools', 'tests', 'docs', 'packaging', 'LICENSES', '.github'}
KERNEL_FILES = {'Makefile', 'dkms.conf', 'minifuze2_ctl.c', 'minifuze2_selection.h', 'minifuze2_uapi.h'}
BINARIES = ('build/minifuze2', 'build/minifuze2-engine')


def version(value):
    match = VERSION_RE.fullmatch(value)
    if not match or (match[4] and any(s.isdigit() and len(s) > 1 and s[0] == '0' for s in match[4].split('.'))):
        raise ValueError('Invalid release version: '+value)
    return value


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    path = PurePosixPath(name)
    if not name or not path.parts or path.is_absolute() or '..' in path.parts or str(path) != name or any(ord(c) < 32 for c in name) or '\\' in name:
        raise ValueError('Unsafe release path: '+repr(name))
    return name


def source_file(name):
    path = PurePosixPath(safe_name(name))
    return (name in ROOT_FILES or (path.parts[0] in SOURCE_DIRS and len(path.parts) > 1)
            or (len(path.parts) == 2 and path.parts[0] == 'kernel' and path.name in KERNEL_FILES))


def manifest_files(data):
    result = {}
    for line in data.decode('utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  \./(.+)', line)
        if not match:
            raise ValueError('Invalid internal SHA256SUMS entry')
        name = safe_name(match[2])
        if name in result:
            raise ValueError('Duplicate checksum entry: '+name)
        result[name] = match[1]
    return result


def source_inventory(root):
    try:
        git = subprocess.run(['git', '-C', str(root), 'rev-parse', '--show-toplevel'],
                             capture_output=True, text=True, timeout=10)
    except FileNotFoundError:
        git = None
    if git is not None and git.returncode == 0 and Path(git.stdout.strip()).resolve() == root.resolve():
        subprocess.run(['git', '-C', str(root), 'diff', '--quiet', 'HEAD', '--'], check=True, timeout=10)
        names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z'], timeout=10).decode().split('\0')
        revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True, timeout=10).strip()
        epoch = int(subprocess.check_output(['git', '-C', str(root), 'show', '-s', '--format=%ct', 'HEAD'], text=True, timeout=10))
    elif (root/'SHA256SUMS').is_file() and (root/'BUILD-INFO.json').is_file():
        sums = manifest_files((root/'SHA256SUMS').read_bytes())
        for name, checksum in sums.items():
            if (root/name).is_symlink() or digest((root/name).read_bytes()) != checksum:
                raise ValueError('Source archive checksum mismatch: '+name)
        metadata = json.loads((root/'BUILD-INFO.json').read_text())
        names, revision, epoch = list(sums), metadata['source_revision'], metadata['source_date_epoch']
    else:
        raise ValueError('Package from a clean Git checkout or a verified source archive')
    return sorted(name for name in names if name and source_file(name)), revision, epoch


def package(args):
    root = ROOT
    release_version = version((root/'VERSION').read_text().strip())
    names, revision, epoch = source_inventory(root)
    if not ROOT_FILES <= set(names) or not {'kernel/'+name for name in KERNEL_FILES} <= set(names):
        raise ValueError('Incomplete release source inventory')
    platform = os.environ.get('MF_PACKAGE_PLATFORM', 'linux-'+os.uname().machine) if args.binary else 'source'
    if not re.fullmatch(r'[A-Za-z0-9_.-]+', platform) or (args.binary and platform == 'source'):
        raise ValueError('Invalid package platform')
    directory = (args.output_dir or root/'dist').resolve()
    directory.mkdir(parents=True, exist_ok=True)
    name = 'minifuze2-'+release_version
    archive = directory/(name+'-'+platform+'.tar.gz')
    checksum = archive.with_name(archive.name+'.sha256')
    if archive.exists() or checksum.exists():
        raise ValueError('Archive already exists; retain it or select another output directory')
    files = {}
    for relative in names:
        path = root/relative
        if path.is_symlink() or not path.is_file():
            raise ValueError('Release source must be a regular file: '+relative)
        files[relative] = (path.read_bytes(), 0o755 if path.stat().st_mode & 0o111 else 0o644)
    if args.binary:
        with tempfile.TemporaryDirectory(prefix='minifuze-strip-') as scratch:
            for relative in BINARIES:
                path = root/relative
                actual = subprocess.check_output([str(path), '--version'], text=True, timeout=10).strip()
                if actual != release_version:
                    raise ValueError(f'Stale binary {relative}: {actual}; expected {release_version}. Run make.')
                copied = Path(scratch)/path.name
                copied.write_bytes(path.read_bytes())
                subprocess.run(['strip', '--strip-debug', str(copied)], check=True, timeout=30)
                files[relative] = (copied.read_bytes(), 0o755)
    module = re.search(r'^PACKAGE_VERSION="([^"]+)"$', (root/'kernel/dkms.conf').read_text(), re.M)
    if not module:
        raise ValueError('Missing control module version')
    metadata = {'application_version': release_version, 'control_module_version': module[1],
                'source_revision': revision, 'source_date_epoch': epoch, 'platform': platform}
    files['BUILD-INFO.json'] = ((json.dumps(metadata, indent=2, sort_keys=True)+'\n').encode(), 0o644)
    sums = ''.join(f'{digest(data)}  ./{relative}\n' for relative, (data, _) in sorted(files.items()))
    files['SHA256SUMS'] = (sums.encode(), 0o644)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.release-', suffix='.tmp', dir=directory, delete=False) as stream:
            temporary = Path(stream.name)
            with gzip.GzipFile(filename='', mode='wb', fileobj=stream, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as tar:
                    for relative, (data, mode) in sorted(files.items()):
                        info = tarfile.TarInfo(name+'/'+relative)
                        info.size, info.mode, info.mtime = len(data), mode, epoch
                        info.uid = info.gid = 0
                        tar.addfile(info, io.BytesIO(data))
        os.link(temporary, archive)
        with checksum.open('x') as stream:
            stream.write(digest(archive.read_bytes())+'  '+archive.name+'\n')
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print('Created '+str(archive))


def verify(archive, expected_version=None):
    sidecar = archive.with_name(archive.name+'.sha256')
    if sidecar.read_text() != digest(archive.read_bytes())+'  '+archive.name+'\n':
        raise ValueError('Archive checksum mismatch: '+archive.name)
    files = {}
    with tarfile.open(archive, 'r:gz') as tar:
        members = tar.getmembers()
        if len(members) > 4096 or sum(item.size for item in members) > 256*1024*1024:
            raise ValueError('Unexpected release archive size')
        for item in members:
            safe_name(item.name)
            if not item.isfile() or item.name in files:
                raise ValueError('Duplicate or non-regular archive member: '+item.name)
            files[item.name] = tar.extractfile(item).read()
    roots = {PurePosixPath(name).parts[0] for name in files}
    if len(roots) != 1:
        raise ValueError('Expected one archive root')
    prefix = roots.pop()+'/'
    files = {name.removeprefix(prefix): data for name, data in files.items()}
    release_version = version(files['VERSION'].decode().strip())
    if prefix != 'minifuze2-'+release_version+'/' or (expected_version and release_version != expected_version):
        raise ValueError('Archive version mismatch')
    metadata = json.loads(files['BUILD-INFO.json'])
    if metadata['application_version'] != release_version or not re.fullmatch(r'[0-9a-f]{40,64}', metadata['source_revision']):
        raise ValueError('Invalid build metadata')
    if archive.name != f'minifuze2-{release_version}-{metadata["platform"]}.tar.gz':
        raise ValueError('Archive name and build platform disagree')
    sums = manifest_files(files.pop('SHA256SUMS'))
    if set(sums) != set(files) or any(digest(data) != sums[name] for name, data in files.items()):
        raise ValueError('Internal file checksum mismatch')
    if not ROOT_FILES <= set(files) or not {'kernel/'+name for name in KERNEL_FILES} <= set(files):
        raise ValueError('Required release files missing')
    binaries = set(BINARIES) & set(files)
    if (metadata['platform'] == 'source' and binaries) or (metadata['platform'] != 'source' and binaries != set(BINARIES)):
        raise ValueError('Binary inventory disagrees with platform')
    if any(name not in {'BUILD-INFO.json', *BINARIES} and not source_file(name) for name in files):
        raise ValueError('Unexpected release file')
    return {'archive': archive.name, 'version': release_version, 'platform': metadata['platform'],
            'source_revision': metadata['source_revision'], 'files': len(files), 'verified': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    pack = sub.add_parser('package')
    pack.add_argument('--binary', action='store_true')
    pack.add_argument('--output-dir', type=Path)
    check = sub.add_parser('verify')
    check.add_argument('archives', nargs='+', type=Path)
    check.add_argument('--version', type=version)
    classify = sub.add_parser('classify')
    classify.add_argument('tag')
    classify.add_argument('--github-output', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'package':
            package(args)
        elif args.command == 'verify':
            for archive in args.archives:
                print(json.dumps(verify(archive, args.version), sort_keys=True))
        else:
            release_version = version((ROOT/'VERSION').read_text().strip())
            if args.tag != 'v'+release_version:
                raise ValueError('Tag must match VERSION')
            prerelease = '-' in release_version
            values = {'version': release_version, 'prerelease': str(prerelease).lower(),
                      'draft': str(not prerelease).lower()}
            if args.github_output:
                with args.github_output.open('a') as stream:
                    stream.write(''.join(f'{key}={value}\n' for key, value in values.items()))
            print(json.dumps(values, sort_keys=True))
    except (OSError, ValueError, KeyError, tarfile.TarError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    main()
