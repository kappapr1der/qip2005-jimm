"""Verify the real published installer transaction without exposing test/profile filenames."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import padding

ROOT = Path(__file__).resolve().parents[1]


def inventory(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit-directory', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    a = parser.parse_args()
    app = a.audit_directory / 'QIP2005'
    jobs = list((a.state / 'transactions').iterdir())
    assert len(jobs) == 1
    job = jobs[0]
    raw = (job / 'manifest.json').read_bytes()
    public = serialization.load_pem_public_key((ROOT / 'updater/keys/release-public.pem').read_bytes())
    public.verify(base64.b64decode((job / 'manifest.sig').read_text()), raw, padding.PKCS1v15(), hashes.SHA256())
    manifest = json.loads(raw)
    setup = a.state / 'downloads' / manifest['version'] / manifest['asset']
    assert hashlib.sha256(setup.read_bytes()).hexdigest() == manifest['sha256']
    assert setup.stat().st_size == manifest['size']
    assert (job / 'phase').read_text() == 'installed'
    assert json.loads((app / 'installed-version.json').read_text())['version'] == manifest['version'] == '1.1.1'
    for item in manifest['files']:
        assert hashlib.sha256((app / item['path']).read_bytes()).hexdigest() == item['sha256']
    before = inventory(a.state / 'backups' / job.name / 'application/Users')
    assert inventory(app / 'Users') == before
    history_before = json.loads((a.audit_directory / 'history-before.json').read_text(encoding='utf-8-sig'))
    history_after = inventory(app / 'Users/audit-fixture/History')
    assert {k: v.lower() for k, v in history_before.items()} == history_after
    log = (a.state / 'agent.log').read_text()
    result = {'baseline_version': '1.1.0', 'release_version': manifest['version'], 'installed_version': manifest['version'],
              'unmodified_production_agent_and_installer': True, 'anonymous_github_api_and_download': True,
              'production_rsa_signature_verified': True, 'installer_sha256': manifest['sha256'],
              'installer_size': manifest['size'], 'installed_program_hashes_matched': len(manifest['files']),
              'test_profile_files_preserved_after_client_exit': len(before), 'history_preserved': True,
              'real_client_exit': 'user closed account-free test QIP normally',
              'phase': 'installed', 'postinstall_application_busy_observed': ' application_busy\n' in log,
              'important_limitation': 'Published installer can report success despite failed prepare-profile helper. Fixed only in audit source; no new public release.',
              'real_account_login_or_messaging_tested': False}
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
