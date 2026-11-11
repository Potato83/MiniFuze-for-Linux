"""DSP and on-disk compatibility checks for the studio mixer."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/minifuze2-engine'


class StudioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='mf-studio-unit-')
        self.addCleanup(self.temp.cleanup)
        self.file = Path(self.temp.name) / 'mix.json'
        self.state = json.loads(subprocess.check_output([BIN, 'factory'], timeout=10))

    def matrix(self, value, success=True):
        self.file.write_text(json.dumps(value))
        run = subprocess.run([BIN, 'matrix', self.file], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode == 0, success, run.stderr)
        return json.loads(run.stdout) if success else None

    def test_solo_filters_only_its_mix_and_preserves_mute(self):
        mix = self.state['mixes'][0]
        for strip in mix['strips']:
            strip['mute'] = False
        before = self.matrix(self.state)
        mix['strips'][0]['solo'] = True
        after = self.matrix(self.state)
        for row in range(2):
            self.assertEqual(after[row][0], before[row][0])
            self.assertEqual(after[row][1:], [0] * 9)
        self.assertEqual(after[2:], before[2:])
        mix['strips'][0]['mute'] = True
        self.assertEqual(self.matrix(self.state)[:2], [[0] * 10] * 2)
        mix['strips'][0]['solo'] = False
        self.assertEqual(self.matrix(self.state)[0][2:], before[0][2:])

    def test_multiple_solos_and_dim_do_not_change_stored_gain(self):
        mix = self.state['mixes'][0]
        mix['strips'][0].update(mute=False, solo=True)
        mix['strips'][2]['solo'] = True
        before = self.matrix(self.state)
        mix['dim'] = True
        after = self.matrix(self.state)
        for row in range(2):
            for actual, expected in zip(after[row], before[row]):
                self.assertAlmostEqual(actual, expected * .1, delta=2e-7)
        self.assertEqual(after[2:], before[2:])
        self.assertEqual(mix['master_db'], 0)
        mix['master_mute'] = True
        self.assertEqual(self.matrix(self.state)[:2], [[0] * 10] * 2)

    def test_new_listen_controls_require_booleans(self):
        for target, key in [(self.state['mixes'][0], 'dim'),
                            (self.state['mixes'][0], 'master_mute'),
                            (self.state['mixes'][0]['strips'][0], 'solo')]:
            original = target[key]
            for bad in [1, 'true', None, []]:
                target[key] = bad
                self.matrix(self.state, success=False)
            target[key] = original

    def test_legacy_mixer_has_identical_coefficients(self):
        legacy = copy.deepcopy(self.state)
        legacy['version'] = 1
        for mix in legacy['mixes']:
            del mix['master_mute'], mix['dim']
            for strip in mix['strips']:
                del strip['solo']
        self.assertEqual(self.matrix(legacy), self.matrix(self.state))

    def document(self):
        return {'version': 4, 'mixer': self.state, 'mfp_template': None,
                'peakrefs': [[0] * 7 for _ in range(3)],
                'presentation': {'links': [False] * 3, 'virtual_visible': [[True] * 3 for _ in range(3)],
                                 'input_names': ['Гитара', '', '', '', '', ''],
                                 'mix_names': ['Мониторинг', '', '']}}

    def test_native_presentation_works_without_windows_template(self):
        doc = self.document()
        self.assertEqual(self.matrix(doc), self.matrix(self.state))
        doc['presentation']['links'][0] = True
        doc['mixer']['mixes'][0]['strips'][0]['level_db'] = -12
        self.matrix(doc, success=False)

    def test_invalid_presentation_is_rejected(self):
        for mutate in [lambda d: d.pop('presentation'),
                       lambda d: d.update(presentation=None),
                       lambda d: d['presentation'].update(links=[0, False, False]),
                       lambda d: d['presentation'].update(virtual_visible=[[True], [True]*3, [True]*3]),
                       lambda d: d['presentation'].update(input_names=['x']*5),
                       lambda d: d['presentation']['input_names'].__setitem__(0, 'x'*129),
                       lambda d: d['presentation']['mix_names'].__setitem__(0, 'a\nb')]:
            doc = self.document()
            mutate(doc)
            self.matrix(doc, success=False)

    def test_windows_export_refuses_unrepresentable_listen_state(self):
        template = ROOT / 'tests/fixtures/mf2-factory.mfp'
        for key in ['dim', 'master_mute']:
            state = copy.deepcopy(self.state)
            state['mixes'][0][key] = True
            self.file.write_text(json.dumps(state))
            run = subprocess.run([BIN, 'preset-export', self.file, template], capture_output=True, timeout=10)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn(b'Turn off Solo', run.stderr)


if __name__ == '__main__':
    unittest.main()
