"""Read only signing-secret metadata; never load, upload or retrieve a private key."""
import argparse
import importlib.util
import json
from pathlib import Path

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).with_name('publish-release.py')
    spec = importlib.util.spec_from_file_location('qip_publisher', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    api = module.GitHub()
    scopes = {'repository': '/repos/' + module.REPO + '/actions/secrets',
              'release_environment': '/repos/' + module.REPO + '/environments/qip-release/secrets',
              'private_build_environment': '/repos/' + module.REPO + '/environments/qip-private-build/secrets'}
    result = {}
    for name, path in scopes.items():
        try:
            visible = api.request(path + '?per_page=1', missing=True)
            present = api.request(path + '/MANIFEST_SIGNING_KEY_B64', missing=True) if visible is not None else None
            result[name] = {'metadata_readable': visible is not None,
                            'legacy_signing_secret_present': present is not None if visible is not None else None}
        except RuntimeError as error:
            result[name] = {'metadata_readable': False, 'error': str(error)}
    result['private_key_loaded_or_uploaded'] = False
    result['limitation'] = 'Checks current named-secret metadata only; cannot prove absence from historical/deleted secrets or other storage.'
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
