# SPDX-License-Identifier: MIT
"""Check coverage and format compatibility for the built-in desktop catalog."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
STRING = r'"(?:[^"\\]|\\.)*"'


def decode_literals(value):
    return ''.join(ast.literal_eval(token) for token in re.findall(STRING, value))


class DesktopTranslations(unittest.TestCase):
    def test_every_literal_message_has_an_english_translation(self):
        source = (ROOT/'native/desktop/main.c').read_text()
        catalog = (ROOT/'native/desktop/i18n.h').read_text()
        entries = re.findall(r'\{\s*('+STRING+r')\s*,\s*('+STRING+r')\s*\}', catalog)
        messages = {ast.literal_eval(ru): ast.literal_eval(en) for ru, en in entries}
        self.assertEqual(len(entries), len(messages), 'duplicate message IDs')
        uses = re.findall(r'\btr\(\s*('+STRING+r'(?:\s*'+STRING+r')*)\s*\)', source)
        used = {decode_literals(value) for value in uses}
        self.assertFalse(used - messages.keys(), 'missing translations: '+repr(used - messages.keys()))
        self.assertTrue(all(messages[key] for key in used))
        # Translators must retain printf arguments and their order/types.
        specifier = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+)?[hljztL]*[diuoxXfFeEgGaAcsp]')
        for ru, en in messages.items():
            self.assertEqual(specifier.findall(ru), specifier.findall(en), ru)


if __name__ == '__main__':
    unittest.main()
