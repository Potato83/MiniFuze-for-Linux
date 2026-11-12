"""Release archive/provenance checks in private Git repositories."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='minifuze-package-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)/'source'
        self.root.mkdir()
        for name in ('VERSION', 'Makefile', 'README.md', 'README.ru.md', 'CONTRIBUTING.md',
                     'CHANGELOG.md', 'TODO.md', 'NOTICE.md', 'LICENSE', '.gitignore', '.gitattributes',
                     'kernel/Makefile', 'kernel/dkms.conf', 'kernel/minifuze2_ctl.c',
                     'kernel/minifuze2_selection.h', 'kernel/minifuze2_uapi.h',
                     'tools/release.py', 'tools/package_release.sh'):
            target = self.root/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT/name, target)
        (self.root/'docs').mkdir()
        (self.root/'docs/guide.md').write_text('Public guide\n')
        self.git('init', '-q')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Release Test')
        self.commit()

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.STDOUT)

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')

    def run_tool(self, *args, success=True, root=None, env=None):
        folder = root or self.root
        run = subprocess.run([sys.executable, str(folder/'tools/release.py'), *map(str, args)],
                             capture_output=True, text=True, timeout=15, env=env)
        if success:
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
        else:
            self.assertNotEqual(run.returncode, 0)
        return run

    def package(self, name='dist'):
        output = Path(self.temp.name)/name
        self.run_tool('package', '--output-dir', output)
        return next(output.glob('*.tar.gz'))

    def rewrite_archive(self, archive, change):
        with tarfile.open(archive, 'r:gz') as tar:
            contents = [(item, tar.extractfile(item).read()) for item in tar.getmembers()]
        with tarfile.open(archive, 'w:gz') as tar:
            change(tar, contents)
        archive.with_name(archive.name+'.sha256').write_text(
            hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')

    def test_source_is_reproducible_excludes_untracked_and_repackages_without_git(self):
        (self.root/'docs/private.json').write_text('{"secret":"not for release"}')
        first, second = self.package('first'), self.package('second')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        result = json.loads(self.run_tool('verify', first).stdout)
        self.assertTrue(result['verified'])
        unpack = Path(self.temp.name)/'unpack'
        with tarfile.open(first, 'r:gz') as tar:
            self.assertFalse(any('private.json' in item.name for item in tar.getmembers()))
            tar.extractall(unpack, filter='data')
        source = next(unpack.iterdir())
        no_git = Path(self.temp.name)/'no-git'
        no_git.mkdir()
        self.run_tool('package', '--output-dir', Path(self.temp.name)/'repacked', root=source,
                      env=dict(os.environ, PATH=str(no_git)))
        self.assertEqual(first.read_bytes(), next((Path(self.temp.name)/'repacked').glob('*.tar.gz')).read_bytes())
        (source/'docs/guide.md').write_text('Unexpected edit')
        self.run_tool('package', '--output-dir', Path(self.temp.name)/'changed', root=source, success=False)

    def test_dirty_sources_symlinks_and_archive_overwrite_are_refused(self):
        archive = self.package()
        self.run_tool('package', '--output-dir', archive.parent, success=False)
        (self.root/'docs/guide.md').write_text('Edited')
        self.run_tool('package', '--output-dir', Path(self.temp.name)/'dirty', success=False)
        self.git('checkout', '--', 'docs/guide.md')
        (self.root/'docs/link.txt').symlink_to('/etc/hostname')
        self.commit()
        self.run_tool('package', '--output-dir', Path(self.temp.name)/'symlink', success=False)

    def test_stale_binary_is_refused(self):
        build = self.root/'build'
        build.mkdir()
        for name in ('minifuze2', 'minifuze2-engine'):
            path = build/name
            path.write_text('#!/bin/sh\nprintf "0.0.0\\n"\n')
            path.chmod(0o755)
        run = self.run_tool('package', '--binary', '--output-dir', Path(self.temp.name)/'binary', success=False)
        self.assertIn('Stale binary', run.stderr)

    def test_binary_archive_contains_runnable_matching_versions(self):
        build = self.root/'build'
        build.mkdir()
        for name in ('minifuze2', 'minifuze2-engine'):
            shutil.copy2(ROOT/'build'/name, build/name)
        output = Path(self.temp.name)/'binary'
        self.run_tool('package', '--binary', '--output-dir', output)
        archive = next(output.glob('*.tar.gz'))
        self.run_tool('verify', archive)
        unpack = Path(self.temp.name)/'binary-unpack'
        with tarfile.open(archive, 'r:gz') as tar:
            tar.extractall(unpack, filter='data')
        source = next(unpack.iterdir())
        for name in ('minifuze2', 'minifuze2-engine'):
            actual = subprocess.check_output([str(source/'build'/name), '--version'], text=True, timeout=5).strip()
            self.assertEqual(actual, (self.root/'VERSION').read_text().strip())

    def test_checksums_and_version_are_verified(self):
        archive = self.package()
        self.run_tool('verify', archive, '--version', '9.9.9', success=False)
        with archive.open('ab') as stream:
            stream.write(b'changed')
        run = self.run_tool('verify', archive, success=False)
        self.assertIn('Archive checksum mismatch', run.stderr)

    def test_internal_tampering_is_rejected_even_with_updated_archive_checksum(self):
        archive = self.package()
        def change(tar, contents):
            for item, data in contents:
                if item.name.endswith('/docs/guide.md'):
                    data = b'changed'; item.size = len(data)
                tar.addfile(item, io.BytesIO(data))
        self.rewrite_archive(archive, change)
        run = self.run_tool('verify', archive, success=False)
        self.assertIn('Internal file checksum mismatch', run.stderr)

    def test_unsafe_archive_members_are_refused(self):
        archive = self.package()
        def change(tar, contents):
            for item, data in contents:
                tar.addfile(item, io.BytesIO(data))
            item = tarfile.TarInfo('../outside.txt'); item.size = 1
            tar.addfile(item, io.BytesIO(b'x'))
        self.rewrite_archive(archive, change)
        self.assertIn('Unsafe release path', self.run_tool('verify', archive, success=False).stderr)

    def test_stable_tags_prepare_drafts_and_candidates_are_prereleases(self):
        for value, expected in [('0.1.0', ('true', 'false')), ('0.1.0-rc.2', ('false', 'true'))]:
            (self.root/'VERSION').write_text(value+'\n')
            result = json.loads(self.run_tool('classify', 'v'+value).stdout)
            self.assertEqual((result['draft'], result['prerelease']), expected)
        self.run_tool('classify', 'v9.9.9', success=False)
        for value in ('01.1.0', '0.1.0-rc.01', '0.1.0\ninvalid'):
            (self.root/'VERSION').write_text(value+'\n')
            self.run_tool('classify', 'v'+value, success=False)


if __name__ == '__main__':
    unittest.main()
