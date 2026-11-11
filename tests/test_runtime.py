"""Opt-in integration checks on a private PipeWire server, with no hardware monitors.

Run: dbus-run-session -- env MINIFUZE_RUNTIME_TESTS=1 make check
"""
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/minifuze2-engine'


@unittest.skipUnless(os.environ.get('MINIFUZE_RUNTIME_TESTS') == '1', 'private audio integration is opt-in')
class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix='mf-studio-runtime-'))
        cls.processes, cls.logs = [], []
        cls.env = dict(os.environ)
        for name in ['XDG_RUNTIME_DIR', 'XDG_CONFIG_HOME', 'XDG_STATE_HOME']:
            path = cls.folder / name.lower()
            path.mkdir(mode=0o700)
            cls.env[name] = str(path)
        cls.env['PIPEWIRE_RUNTIME_DIR'] = cls.env['XDG_RUNTIME_DIR']
        cls.env.pop('PIPEWIRE_REMOTE', None)
        cls.addClassCleanup(cls.cleanup_audio)
        help_text = subprocess.check_output(['wireplumber', '--help'], text=True)
        policy = '--profile' in help_text
        if not policy and list(Path('/dev/snd').glob('controlC*')):
            raise unittest.SkipTest('WirePlumber 0.5 policy profile required when hardware is present')
        cls.start(['pipewire'], 'pipewire')
        deadline = time.monotonic() + 10
        while not (Path(cls.env['XDG_RUNTIME_DIR']) / 'pipewire-0').exists():
            if time.monotonic() > deadline:
                raise RuntimeError('Private PipeWire startup timed out')
            time.sleep(.05)
        cls.start(['wireplumber', '--profile=policy'] if policy else ['wireplumber'], 'wireplumber')
        time.sleep(.5)

    @classmethod
    def start(cls, argv, name):
        log = (cls.folder / (name + '.log')).open('a')
        cls.logs.append(log)
        process = subprocess.Popen(list(map(str, argv)), env=cls.env, stdout=log, stderr=subprocess.STDOUT)
        cls.processes.append(process)
        return process

    @classmethod
    def cleanup_audio(cls):
        for process in reversed(cls.processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        for log in cls.logs:
            log.close()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix='case-', dir=self.folder))
        self.prefix = 'mf_desktop_test_' + self.case.name.replace('-', '_')
        self.ready = self.case / 'ready.json'
        self.engine = None
        self.addCleanup(self.stop_engine)
        self.start_engine()

    def start_engine(self):
        self.ready.unlink(missing_ok=True)
        self.engine = self.start([BIN, 'panel', '--prefix', self.prefix,
                                  '--state', self.case/'saved.json', '--session-state', self.case/'working.json',
                                  '--ready-file', self.ready, '--ctl', self.case/'no-hardware-control'], self.case.name)
        deadline = time.monotonic() + 20
        while not self.ready.exists():
            if self.engine.poll() is not None:
                self.fail((self.folder/(self.case.name+'.log')).read_text())
            if time.monotonic() > deadline:
                self.fail('Engine readiness timed out')
            time.sleep(.05)
        self.url = json.loads(self.ready.read_text())['url']
        with urllib.request.urlopen(self.url, timeout=5) as response:
            html = response.read().decode()
        self.token = re.search(r'name="csrf-token" content="([a-f0-9]{64})"', html).group(1)
        self.assertIsNone(self.state()['hardware'])
        self.assertFalse(self.state()['devices'])

    def stop_engine(self):
        if self.engine and self.engine.poll() is None:
            self.engine.terminate()
            self.engine.wait(timeout=15)

    def state(self):
        with urllib.request.urlopen(self.url+'/api/state', timeout=10) as response:
            return json.load(response)

    def command(self, path, body=None, status=200):
        request = urllib.request.Request(self.url+'/api/'+path,
            data=json.dumps(body if body is not None else {}).encode(),
            headers={'Content-Type': 'application/json', 'Origin': self.url, 'X-CSRF-Token': self.token})
        try:
            with urllib.request.urlopen(request, timeout=15) as response:
                self.assertEqual(response.status, status)
                return json.load(response)
        except urllib.error.HTTPError as error:
            self.assertEqual(error.code, status, error.read().decode())
            error.close()
            return None

    def test_native_metadata_history_and_file_roundtrip(self):
        initial = self.state()
        self.assertTrue(initial['preset']['editable'])
        self.assertFalse(initial['preset']['has_template'])
        self.command('rename', {'kind': 'input', 'index': 0, 'name': 'Гитара'})
        self.command('link', {'mix': 0, 'enabled': True})
        self.command('virtual', {'mix': 1, 'index': 2, 'visible': False})
        self.command('peakref', {'mix': 0, 'source': 0, 'db': -18})
        exported = json.loads(self.command('export-native')['text'])
        self.assertIsNone(exported['mfp_template'])
        self.assertEqual(exported['presentation']['input_names'][0], 'Гитара')
        self.command('factory')
        restored = self.command('import-native', {'text': json.dumps(exported)})
        self.assertEqual(restored['preset']['input_names'][0], 'Гитара')
        self.assertTrue(restored['preset']['links'][0])
        self.assertEqual(restored['peakref']['db'][0][:2], [-18, -18])
        self.assertFalse(restored['preset']['virtual_visible'][1][2])
        self.command('undo')
        self.assertEqual(self.state()['preset']['input_names'][0], '')
        self.command('redo')
        self.assertEqual(self.state()['preset']['input_names'][0], 'Гитара')
        self.command('export-mfp', status=400)

    def test_unsaved_mix_survives_service_restart_and_remains_dirty(self):
        self.command('save')
        state = self.state()['mixer']
        state['mixes'][0]['strips'][2].update(level_db=-17.5, solo=True)
        state['mixes'][0]['dim'] = True
        self.command('mixer', state)
        self.command('rename', {'kind': 'mix', 'index': 0, 'name': 'Запись гитары'})
        self.stop_engine()
        self.start_engine()
        restored = self.state()
        self.assertEqual(restored['mixer'], state)
        self.assertTrue(restored['dirty'])
        self.assertFalse(restored['can_undo'])
        self.assertEqual(restored['preset']['mix_names'][0], 'Запись гитары')

    def test_undo_restores_saved_state_and_new_edit_clears_redo(self):
        mixer = self.state()['mixer']
        mixer['mixes'][0]['strips'][2]['level_db'] = -9.5
        self.command('mixer', mixer)
        self.command('save')
        mixer['mixes'][0]['strips'][2]['level_db'] = -20
        self.command('mixer', mixer)
        undone = self.command('undo')
        self.assertFalse(undone['dirty'])
        self.assertTrue(undone['can_redo'])
        self.command('rename', {'kind': 'input', 'index': 1, 'name': 'Микрофон'})
        self.assertFalse(self.state()['can_redo'])

    def test_invalid_import_and_hardware_commands_leave_mix_unchanged(self):
        before = self.state()
        self.command('import-native', {'text': '{"version":4}'}, status=400)
        self.command('inst', {'channel': 1, 'enabled': True}, status=400)
        self.assertEqual(self.state()['mixer'], before['mixer'])
        self.assertFalse(self.state()['can_undo'])
        self.assertIsNone(self.state()['hardware'])

    def test_link_solo_and_hidden_channel_do_not_leave_silent_solo(self):
        self.command('link', {'mix': 0, 'enabled': True})
        mixer = self.state()['mixer']
        mixer['mixes'][0]['strips'][0]['solo'] = True
        state = self.command('mixer', mixer)
        self.assertTrue(state['mixer']['mixes'][0]['strips'][1]['solo'])
        mixer = state['mixer']
        mixer['mixes'][1]['strips'][5]['solo'] = True
        self.command('mixer', mixer)
        state = self.command('virtual', {'mix': 1, 'index': 2, 'visible': False})
        self.assertFalse(state['mixer']['mixes'][1]['strips'][5]['solo'])

    def test_history_is_bounded(self):
        for n in range(35):
            self.command('rename', {'kind': 'mix', 'index': 0, 'name': 'Микс '+str(n)})
        for _ in range(32):
            self.command('undo')
        self.assertFalse(self.state()['can_undo'])
        self.command('undo', status=400)


if __name__ == '__main__':
    unittest.main()
