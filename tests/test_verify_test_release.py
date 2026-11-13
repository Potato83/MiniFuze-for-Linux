"""The release must contain every checked download and reject tampering."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_test_release', ROOT/'tools/verify_test_release.py')
verify_release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_release)


class ReleaseInventoryTests(unittest.TestCase):
    def test_checksums_and_inventory_are_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in verify_release.NAMES:
                data = name.encode()
                (folder/name).write_bytes(data)
                (folder/(name+'.sha256')).write_text(hashlib.sha256(data).hexdigest()+'  '+name+'\n')
            verify_release.verify([folder])
            name = next(iter(verify_release.NAMES))
            (folder/name).write_text('corrupt')
            with self.assertRaises(ValueError):
                verify_release.verify([folder])
            (folder/name).unlink()
            with self.assertRaises(ValueError):
                verify_release.verify([folder])
