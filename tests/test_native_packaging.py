"""Native package boundaries, pinned sources and refusal paths (no root/audio)."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/f'tools/{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class NativePackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='minifuze-native-unit-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.builder = load('package_native')
        self.stage = load('stage_native')

    def test_dkms_stages_sources_and_policy_without_autoload_or_user_config(self):
        dest = self.work/'dkms'
        self.stage.stage(ROOT, ROOT/'packaging/native', dest, 'dkms', self.builder.UPSTREAM)
        names = {p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file()}
        self.assertIn('usr/src/minifuze2-ctl-0.2.0/minifuze2_ctl.c', names)
        self.assertIn('usr/bin/minifuze2-ctl-setup', names)
        self.assertTrue(all(name.startswith('usr/') for name in names))
        self.assertFalse(any('modules-load.d' in name or name.endswith('.ko') for name in names))
        self.assertEqual((dest/'usr/bin/minifuze2-ctl-setup').stat().st_mode & 0o777, 0o755)
        config = (dest/'usr/src/minifuze2-ctl-0.2.0/dkms.conf').read_text()
        self.assertIn('MAKE[0]="make KDIR=/lib/modules/${kernelver}/build"', config)

    def test_staging_refuses_nonempty_directory_and_mismatched_module_version(self):
        dest = self.work/'nonempty'
        dest.mkdir()
        (dest/'keep').write_text('retained')
        with self.assertRaises(ValueError):
            self.stage.stage(ROOT, ROOT/'packaging/native', dest, 'dkms', self.builder.UPSTREAM)
        wrong = dict(self.builder.UPSTREAM, control_module_version='99.0.0')
        with self.assertRaises(ValueError):
            self.stage.stage(ROOT, ROOT/'packaging/native', self.work/'wrong', 'dkms', wrong)
        self.assertEqual((dest/'keep').read_text(), 'retained')

    def test_arch_recipes_pin_source_and_all_local_file_hashes(self):
        recipes = self.builder.export_arch(self.work, 'arch-linux')
        for folder in recipes:
            text = (folder/'PKGBUILD').read_text()
            self.assertIn(self.builder.UPSTREAM['source_revision'], text)
            self.assertIn(self.builder.UPSTREAM['source_sha256'], text)
            self.assertNotIn('@VERSION@', text)
            for path in folder.iterdir():
                if path.name != 'PKGBUILD':
                    self.assertIn(hashlib.sha256(path.read_bytes()).hexdigest(), text)
            subprocess.run(['bash', '-n', str(folder/'PKGBUILD')], check=True)

    def test_source_extraction_refuses_traversal_and_symlinks(self):
        for name, link in (('../escape', False), ('symlink', True)):
            archive = self.work/'unsafe.tar.gz'
            with tarfile.open(archive, 'w:gz') as tar:
                entry = tarfile.TarInfo('MiniFuze-for-Linux-'+self.builder.UPSTREAM['source_revision']+'/'+name)
                if link:
                    entry.type = tarfile.SYMTYPE
                    entry.linkname = '/etc/passwd'
                tar.addfile(entry, io.BytesIO())
            with self.assertRaises(ValueError):
                self.builder.extract_source(archive, self.work/'unpack')
        self.assertFalse((self.work/'escape').exists())

    def test_artifact_checksum_and_existing_output_are_protected(self):
        source = self.work/'source.pkg'
        source.write_bytes(b'package fixture')
        output = self.work/'output'
        output.mkdir()
        self.builder.copy_artifact(source, output)
        self.assertEqual((output/'source.pkg.sha256').read_text(),
                         hashlib.sha256(source.read_bytes()).hexdigest()+'  source.pkg\n')
        source.write_bytes(b'changed')
        with self.assertRaises(ValueError):
            self.builder.copy_artifact(source, output)
        self.assertEqual((output/'source.pkg').read_bytes(), b'package fixture')

    def test_setup_usage_and_validation_do_not_change_host(self):
        helper = ROOT/'packaging/native/minifuze2-ctl-setup'
        subprocess.run(['bash', '-n', str(helper)], check=True)
        help_run = subprocess.run([str(helper), '--help'], capture_output=True, text=True)
        self.assertEqual(help_run.returncode, 0)
        invalid = subprocess.run([str(helper), '--user', 'root', '--serial', 'ignored'], capture_output=True, text=True)
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn('not root', invalid.stderr)


if __name__ == '__main__':
    unittest.main()
