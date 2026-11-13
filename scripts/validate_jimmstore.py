"""Validate against the exact official JimmStore schema. Read-only HTTP; never submit an app."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import urllib.parse
import urllib.request

from jsonschema import Draft202012Validator, FormatChecker
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_FILE = ROOT / 'build/jimmstore/manifest.schema.json'
LOCK = ROOT / 'scripts/jimmstore-schema.lock.json'
OFFICIAL_SCHEMA = 'https://jimm.online/store/manifest.schema.json'
BINARY_REFERENCE = re.compile(r'(?i)(?:\.(?:exe|dmg|apk|zip|7z|msi|pkg|deb|rpm|msix|appx|tar|gz|xz)(?:$|[/?#\s])|/releases/download/|(?<![a-z0-9])(?:exe|dmg|apk|zip)(?![a-z0-9]))')


def no_duplicate_keys(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError('Duplicate JSON property: ' + key)
        result[key] = value
    return result


def parse_json(raw: bytes):
    return json.loads(raw.decode('utf-8-sig'), object_pairs_hook=no_duplicate_keys)


def get(url: str, limit: int) -> bytes:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.username or parsed.password:
        raise ValueError('Public URLs must use HTTPS without credentials')
    request = urllib.request.Request(url, headers={'User-Agent': 'QIP-JimmStore-Validator/1'})
    with urllib.request.urlopen(request, timeout=30) as response:
        if urllib.parse.urlsplit(response.url).scheme != 'https':
            raise ValueError('Insecure HTTP redirect')
        raw = response.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('Public resource exceeds the validation size limit')
    return raw


def load_schema(path: Path = SCHEMA_FILE, fetch: bool = False):
    lock = parse_json(LOCK.read_bytes())
    if lock['url'] != OFFICIAL_SCHEMA:
        raise ValueError('Only the official JimmStore schema is accepted')
    raw = get(lock['url'], 1024 * 1024) if fetch else path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != lock['sha256']:
        raise ValueError('Official JimmStore schema changed; review the upstream requirements before updating its digest')
    schema = parse_json(raw)
    if schema.get('$id') != OFFICIAL_SCHEMA or schema.get('$schema') != lock['dialect']:
        raise ValueError('Unexpected schema identity or dialect')
    Draft202012Validator.check_schema(schema)
    if fetch:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
    return schema


def validate_image(raw: bytes, square: bool):
    if square and len(raw) > 3 * 1024 * 1024:
        raise ValueError('JimmStore icon exceeds 3 MiB')
    with Image.open(io.BytesIO(raw)) as image:
        width, height = image.size
        image_format = image.format
        image.verify()
    if image_format not in {'PNG', 'JPEG', 'WEBP'}:
        raise ValueError('Use a real PNG, JPEG or WebP image')
    if square and (width != height or width < 128):
        raise ValueError('JimmStore icon must be square and at least 128 x 128')
    return {'format': image_format, 'width': width, 'height': height,
            'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def validate_document(document, schema, root: Path = ROOT):
    checker = FormatChecker(formats=['uri'])
    if 'uri' not in checker.checkers:
        raise ValueError('Install jsonschema[format-nongpl] to validate URI formats')
    errors = sorted(Draft202012Validator(schema, format_checker=checker).iter_errors(document), key=lambda error: str(error.json_path))
    if errors:
        raise ValueError('; '.join(error.json_path + ': ' + error.message for error in errors))
    if document.get('$schema') != OFFICIAL_SCHEMA:
        raise ValueError('Manifest must identify the official schema')
    repository = parse_json((root / 'repository.json').read_bytes())
    repo = repository['owner'] + '/' + repository['name']
    expected_platforms = [{'os': 'windows', 'source': {'type': 'github', 'repo': repo}}]
    if document['platforms'] != expected_platforms:
        raise ValueError('Use this project GitHub source for Windows; do not encode the app version as an OS version')
    if document['category'] != 'client' or document['developer']['name'] != repository['owner']:
        raise ValueError('Category or maintainer differs from the verified project metadata')
    if document.get('license') == 'open-source':
        raise ValueError('The original QIP client is not covered by the maintenance-code MIT license')
    if BINARY_REFERENCE.search(json.dumps(document, ensure_ascii=False)):
        raise ValueError('JimmStore documentation forbids binary/archive references in the manifest')
    asset_prefix = 'https://raw.githubusercontent.com/' + repo + '/main/'
    media = {}
    for index, url in enumerate([document['icon'], *document.get('screenshots', [])]):
        if not url.startswith(asset_prefix):
            raise ValueError('Use actual image resources from this repository')
        relative = urllib.parse.unquote(url[len(asset_prefix):])
        path = (root / relative).resolve()
        if not path.is_relative_to(root.resolve()) or '?' in relative or '#' in relative:
            raise ValueError('Invalid repository image path')
        media[url] = validate_image(path.read_bytes(), square=index == 0)
    version = (root / 'VERSION').read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid project VERSION')
    return {'official_schema': OFFICIAL_SCHEMA, 'schema_sha256': parse_json(LOCK.read_bytes())['sha256'],
            'version_file': version, 'version_source': 'GitHub Releases, resolved by JimmStore',
            'release_url': 'https://github.com/' + repo + '/releases/latest',
            'manifest_raw_url': asset_prefix + 'jimm-app.json', 'media': media,
            'screenshots_count': len(document.get('screenshots', [])), 'submitted_to_store': False}


def validate_public(document, report):
    remote = parse_json(get(report['manifest_raw_url'], 128 * 1024))
    if remote != document:
        raise ValueError('Published Raw manifest differs from the validated local manifest')
    for url, expected in report['media'].items():
        raw = get(url, 3 * 1024 * 1024 if url == document['icon'] else 20 * 1024 * 1024)
        actual = validate_image(raw, square=url == document['icon'])
        if actual != expected:
            raise ValueError('Published media differs from the validated local file')
    report['public_manifest_and_media_verified'] = True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch-schema', action='store_true')
    parser.add_argument('--schema', type=Path, default=SCHEMA_FILE)
    parser.add_argument('--check-public', action='store_true')
    parser.add_argument('--check-latest', action='store_true', help='Use after publication, not for a future unreleased VERSION bump')
    parser.add_argument('--release-tag', help='Verify a GitHub release-event tag against VERSION')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        schema = load_schema(args.schema, args.fetch_schema)
        document = parse_json((ROOT / 'jimm-app.json').read_bytes())
        report = validate_document(document, schema)
        if args.release_tag and args.release_tag != 'v' + report['version_file']:
            raise ValueError('Release event tag differs from VERSION')
        if args.check_public:
            validate_public(document, report)
        if args.check_latest:
            source = document['platforms'][0]['source']['repo']
            release = parse_json(get('https://api.github.com/repos/' + source + '/releases/latest', 512 * 1024))
            if release['tag_name'] != 'v' + report['version_file'] or release['draft'] or release['prerelease']:
                raise ValueError('Current stable GitHub release differs from VERSION')
            report['latest_release_verified'] = release['tag_name']
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, ensure_ascii=True))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')
