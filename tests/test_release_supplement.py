"""The one-time native supplement may only mutate the expected stable draft."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class ReleaseSupplementTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('supplement_release', ROOT/'tools/supplement_release.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)

    def test_published_or_prerelease_is_refused_without_mutation(self):
        for draft, prerelease in ((False, False), (True, True)):
            release = dict(tagName='v0.1.0', isDraft=draft, isPrerelease=prerelease)
            with patch.object(self.module.subprocess, 'check_output', return_value=json.dumps(release)), \
                 patch.object(self.module.subprocess, 'run') as mutate:
                with self.assertRaises(ValueError):
                    self.module.draft()
                mutate.assert_not_called()

    def test_incomplete_or_corrupted_assets_are_refused_before_github(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            with patch('sys.argv', ['supplement', temporary]), \
                 patch.object(self.module.subprocess, 'check_output') as github:
                with self.assertRaises(ValueError):
                    self.module.main()
                github.assert_not_called()
                for name in self.module.NATIVE:
                    (folder/name).write_bytes(b'fixture')
                    (folder/(name+'.sha256')).write_text('incorrect checksum\n')
                with self.assertRaises(ValueError):
                    self.module.main()
                github.assert_not_called()

    def test_moved_original_tag_is_refused_without_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in self.module.NATIVE:
                (folder/name).write_bytes(b'fixture')
                (folder/(name+'.sha256')).write_text(hashlib.sha256(b'fixture').hexdigest()+'  '+name+'\n')
            with patch('sys.argv', ['supplement', temporary]), \
                 patch.object(self.module.subprocess, 'check_output', return_value='0'*40+'\n'), \
                 patch.object(self.module.subprocess, 'run') as mutate:
                with self.assertRaises(ValueError):
                    self.module.main()
                mutate.assert_not_called()

    def test_draft_url_changes_are_allowed_but_original_content_is_retained(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in self.module.NATIVE:
                (folder/name).write_bytes(b'fixture')
                (folder/(name+'.sha256')).write_text(hashlib.sha256(b'fixture').hexdigest()+'  '+name+'\n')
            originals = [dict(name=name, id=name, apiUrl='api/'+name, size=4,
                              createdAt='2026-11-12', url='old-draft/'+name)
                         for name in self.module.ORIGINAL]
            assets = [dict(asset, url='new-draft/'+asset['name']) for asset in originals]
            assets += [dict(name=path.name) for path in folder.iterdir()]
            before = dict(url='old-draft', assets=originals)
            after = dict(url='new-draft', assets=assets)

            def hashes(names):
                return {name: ('original-content-hash' if name in self.module.ORIGINAL else
                               hashlib.sha256((folder/name).read_bytes()).hexdigest()) for name in names}

            with patch('sys.argv', ['supplement', temporary]), \
                 patch.object(self.module.subprocess, 'check_output', return_value=self.module.SOURCE), \
                 patch.object(self.module.subprocess, 'run') as mutate, \
                 patch.object(self.module, 'draft', side_effect=[before]+[after]*18), \
                 patch.object(self.module, 'download_hashes', side_effect=hashes), \
                 patch('sys.stdout', new_callable=io.StringIO):
                self.module.main()
                self.assertEqual(mutate.call_count, 15)  # 14 native uploads + notes.
                self.assertTrue(all(Path(call.args[0][4]).name not in self.module.ORIGINAL
                                    for call in mutate.call_args_list[:14]))

    def test_final_review_rejects_published_release_without_mutation(self):
        with patch.object(self.module.subprocess, 'check_output', return_value=self.module.SOURCE), \
             patch.object(self.module, 'draft', side_effect=ValueError('Published release')), \
             patch.object(self.module.subprocess, 'run') as mutate:
            with self.assertRaises(ValueError):
                self.module.review()
            mutate.assert_not_called()

    def test_final_review_only_updates_notes_and_keeps_every_download(self):
        expected = self.module.ORIGINAL | self.module.NATIVE | {name+'.sha256' for name in self.module.NATIVE}
        release = {'assets': [{'name': name, 'id': name, 'size': 5} for name in expected], 'url': 'draft'}
        hashes = {name: 'verified' for name in expected}
        outputs = [self.module.SOURCE, json.dumps({'body': (ROOT/'docs/validation/release-notes-0.1.0.md').read_text()})]
        with patch.object(self.module.subprocess, 'check_output', side_effect=outputs), \
             patch.object(self.module, 'draft', return_value=release), \
             patch.object(self.module, 'download_hashes', return_value=hashes) as download, \
             patch.object(self.module.subprocess, 'run') as mutate, \
             patch('sys.stdout', new_callable=io.StringIO):
            self.module.review()
            mutate.assert_called_once_with(['gh', 'release', 'edit', 'v0.1.0',
                                            '--notes-file', 'docs/validation/release-notes-0.1.0.md'], check=True)
            self.assertEqual(download.call_count, 2)
            for call in download.call_args_list:
                self.assertEqual(call.args, (expected,))
                self.assertEqual(call.kwargs, {'verify_sidecars': True})
