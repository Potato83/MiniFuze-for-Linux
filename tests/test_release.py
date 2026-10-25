"""Black-box checks against retained numeric references; standard library only."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT/'build/minifuze2-engine'
FIXTURES = ROOT/'tests/fixtures'
REFS = json.loads((FIXTURES/'native-port-reference.json').read_text())


class EngineTests(unittest.TestCase):
    def run_engine(self, *args):
        return subprocess.check_output([str(BIN), *map(str, args)], timeout=10)

    def assert_matrix(self, actual, expected, tolerance=2e-6):
        self.assertEqual(len(actual), len(expected))
        for row, reference in zip(actual, expected):
            self.assertEqual(len(row), len(reference))
            for value, target in zip(row, reference):
                self.assertAlmostEqual(value, target, delta=tolerance)

    def test_matrices_match_retained_vectors(self):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp)/'state.json'
            for case in REFS['mixers']:
                file.write_text(json.dumps(case['state']))
                self.assert_matrix(json.loads(self.run_engine('matrix', file)), case['matrix'], 1.3e-7)

    def test_presets_roundtrip_and_references(self):
        with tempfile.TemporaryDirectory() as temp:
            file, exported = Path(temp)/'state.json', Path(temp)/'export.mfp'
            for case in REFS['presets']:
                fixture = FIXTURES/case['file']
                doc = json.loads(self.run_engine('preset-import', fixture))
                self.assertEqual(doc['peakrefs'], case['references'])
                file.write_text(json.dumps(doc))
                self.assert_matrix(json.loads(self.run_engine('matrix', file)), case['matrix'])
                exported.write_bytes(self.run_engine('preset-export', file, fixture))
                again = json.loads(self.run_engine('preset-import', exported))
                self.assertEqual(again['peakrefs'], doc['peakrefs'])
                self.assertEqual(again['mfp_template']['parameters'].keys(), doc['mfp_template']['parameters'].keys())

    def test_invalid_values_refused(self):
        factory = json.loads(self.run_engine('factory'))
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp)/'bad.json'
            for key, value in [('level_db', True), ('level_db', 15), ('mute', 1), ('pans', [-1])]:
                state = copy.deepcopy(factory)
                state['mixes'][0]['strips'][0][key] = value
                file.write_text(json.dumps(state))
                run = subprocess.run([str(BIN), 'matrix', str(file)], capture_output=True, timeout=10)
                self.assertNotEqual(run.returncode, 0)

    def test_meter_positions(self):
        for case in REFS['meter_positions']:
            value = float(self.run_engine('meter-position', case['amplitude']))
            self.assertAlmostEqual(value, case['position'], delta=1.2e-7)

    def test_desktop_rejects_nonlocal_target_before_gtk(self):
        for url in ['https://example.com', 'http://localhost:39713', 'http://127.0.0.1:39713/elsewhere',
                    'http://user@127.0.0.1:39713', 'http://127.0.0.1:39713?x=1']:
            run = subprocess.run([str(ROOT/'build/minifuze2'), '--url', url], capture_output=True, timeout=5)
            self.assertEqual(run.returncode, 2, (url, run.stderr))


if __name__ == '__main__':
    unittest.main()
