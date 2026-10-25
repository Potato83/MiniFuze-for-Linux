"""Installer transactions in private directories; systemctl is a test double."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='minifuze-install-test-')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.prefix = self.folder/'user files/local'
        self.config = self.folder/'user files/config'
        shim = self.folder/'commands'
        shim.mkdir()
        script = shim/'systemctl'
        script.write_text('''#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$TASK_SYSTEMCTL_LOG"
case "${2:-}" in
  is-active) exit 1 ;;
  enable) [[ ${TASK_FAIL_ENABLE:-0} == 0 ]] ;;
  *) exit 0 ;;
esac
''')
        script.chmod(0o755)
        self.log = self.folder/'systemctl.log'
        self.env = dict(os.environ, PATH=str(shim)+os.pathsep+os.environ['PATH'],
                        TASK_SYSTEMCTL_LOG=str(self.log))
        self.files = [self.prefix/'bin/minifuze2', self.prefix/'bin/minifuze2-engine',
                      self.prefix/'share/icons/hicolor/scalable/apps/io.github.minifuze2.Control.svg',
                      self.prefix/'share/applications/io.github.minifuze2.Control.desktop']
        self.unit = self.config/'systemd/user/minifuze2.service'

    def invoke(self, *args, success=True):
        run = subprocess.run(['bash', str(ROOT/'tools/install_desktop.sh'),
                              '--prefix', str(self.prefix), '--config-home', str(self.config),
                              *args], env=self.env, capture_output=True, text=True, timeout=15)
        if success:
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
        else:
            self.assertNotEqual(run.returncode, 0, run.stdout+run.stderr)
        return run

    def test_reuse_roundtrip_keeps_user_preset_and_no_service_commands(self):
        self.invoke('--reuse-existing')
        for file in self.files:
            self.assertTrue(file.is_file())
        self.assertFalse(self.unit.exists())
        preset = self.prefix/'share/minifuze2/my-preset.json'
        preset.write_text('retained')
        self.invoke('--uninstall')
        self.assertEqual(preset.read_text(), 'retained')
        self.assertFalse(any(file.exists() for file in self.files))
        self.assertFalse(self.log.exists())

    def test_changed_installed_file_refuses_removal(self):
        self.invoke('--reuse-existing')
        with self.files[0].open('ab') as f:
            f.write(b'modified')
        self.invoke('--uninstall', success=False)
        self.assertTrue(all(file.exists() for file in self.files))

    def test_existing_file_is_not_overwritten(self):
        self.files[0].parent.mkdir(parents=True)
        self.files[0].write_text('belongs to someone else')
        self.invoke('--reuse-existing', success=False)
        self.assertEqual(self.files[0].read_text(), 'belongs to someone else')
        self.assertFalse(self.files[1].exists())

    def test_fresh_unit_and_uninstall_are_owned(self):
        self.invoke()
        unit = self.unit.read_text()
        self.assertIn('ExecStart="'+str(self.files[1])+'" panel', unit)
        self.assertIn('PartOf=pipewire.service', unit)
        self.assertIn('--state "%S/minifuze2/mixer.json"', unit)
        self.invoke('--uninstall')
        self.assertFalse(self.unit.exists())
        self.assertIn('--user disable --now minifuze2.service', self.log.read_text())

    def test_enable_failure_rolls_back_every_created_file(self):
        self.env['TASK_FAIL_ENABLE'] = '1'
        self.invoke(success=False)
        self.assertFalse(any(file.exists() for file in [*self.files, self.unit]))
        self.assertFalse((self.prefix/'share/minifuze2/owned-files').exists())
        self.assertFalse((self.prefix/'share/minifuze2/installed.sha256').exists())

    def test_relative_prefix_refused(self):
        self.invoke('--prefix', 'relative', '--reuse-existing', success=False)
        self.assertFalse(self.prefix.exists())


if __name__ == '__main__':
    unittest.main()
