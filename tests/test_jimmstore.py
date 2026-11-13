"""Store contract tests against the downloaded, checksum-pinned official schema."""
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import validate_jimmstore as store


class JimmStoreContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = store.load_schema()
        cls.manifest = store.parse_json((ROOT / 'jimm-app.json').read_bytes())

    def reject(self, edit, message):
        document = deepcopy(self.manifest)
        edit(document)
        with self.assertRaisesRegex(ValueError, message):
            store.validate_document(document, self.schema)

    def test_actual_manifest_uses_github_release_resolution(self):
        report = store.validate_document(self.manifest, self.schema)
        self.assertEqual(report['version_file'], (ROOT / 'VERSION').read_text().strip())
        self.assertEqual(report['release_url'], 'https://github.com/kappapr1der/qip2005-jimm/releases/latest')
        self.assertFalse(report['submitted_to_store'])

    def test_app_version_is_not_an_official_manifest_field(self):
        self.reject(lambda item: item.update(version='1.1.3'), 'Additional properties')

    def test_category_must_use_official_enum(self):
        self.reject(lambda item: item.update(category='ICQ-client'), 'not one of')

    def test_icon_is_required(self):
        self.reject(lambda item: item.pop('icon'), 'required property')

    def test_icon_requires_https(self):
        self.reject(lambda item: item.update(icon=item['icon'].replace('https:', 'http:')), 'does not match')

    def test_summary_length_limit(self):
        self.reject(lambda item: item.update(summary='x' * 161), 'too long')

    def test_github_source_must_match_repository(self):
        def edit(item):
            item['platforms'][0]['source']['repo'] = 'other/project'
        self.reject(edit, 'this project GitHub source')

    def test_os_minimum_must_not_be_used_for_app_version(self):
        def edit(item):
            item['platforms'][0]['min_version'] = '1.1.3'
        self.reject(edit, 'OS version')

    def test_documented_no_binary_links_rule(self):
        self.reject(lambda item: item.update(homepage='https://github.com/kappapr1der/qip2005-jimm/releases/download/v1.1.3/setup.exe'), 'binary/archive')

    def test_documented_no_binary_mentions_rule(self):
        self.reject(lambda item: item.update(description='Download the ZIP archive.'), 'binary/archive')

    def test_original_client_must_not_be_labelled_mit(self):
        self.reject(lambda item: item.update(license='open-source'), 'original QIP')

    def test_duplicate_json_properties_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON property'):
            store.parse_json(b'{"schema":1,"schema":2}')

    def test_upstream_schema_change_requires_review(self):
        with patch.object(Path, 'read_bytes', return_value=b'{"type":"object"}'):
            with patch.object(store, 'parse_json', return_value={
                'url': store.OFFICIAL_SCHEMA, 'sha256': '0' * 64,
            }):
                with self.assertRaisesRegex(ValueError, 'schema changed'):
                    store.load_schema()

    def test_real_icon_matches_recorded_origin(self):
        asset = ROOT / 'assets/jimmstore/qip.png'
        provenance = json.loads((asset.parent / 'provenance.json').read_text())
        raw = asset.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), provenance['output_sha256'])
        self.assertEqual(store.validate_image(raw, square=True)['width'], 128)
        with Image.open(asset) as image:
            original_pixels = image.resize((32, 32), Image.Resampling.NEAREST)
            self.assertEqual(original_pixels.resize((128, 128), Image.Resampling.NEAREST).tobytes(), image.tobytes())

    def test_undersized_icon_is_rejected_even_if_json_is_valid(self):
        data = io.BytesIO()
        Image.new('RGBA', (32, 32)).save(data, format='PNG')
        with self.assertRaisesRegex(ValueError, 'at least 128'):
            store.validate_image(data.getvalue(), square=True)


if __name__ == '__main__':
    unittest.main()
