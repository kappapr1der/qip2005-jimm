"""Fail-closed publication gate; this validates a recorded review, not legal rights."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require_permission(root: Path = ROOT) -> None:
    state = json.loads((root / 'docs/redistribution.json').read_text(encoding='utf-8'))
    if (state.get('qip_redistribution_approved') is not True or
            state.get('verification_status') != 'rightsholder_permission_reviewed'):
        raise RuntimeError('QIP publication blocked: independent rightsholder permission has not been reviewed')
    evidence = state.get('evidence')
    if not isinstance(evidence, str) or not evidence or Path(evidence).is_absolute() or '..' in Path(evidence).parts:
        raise RuntimeError('QIP publication blocked: invalid permission evidence')
    document = (root / evidence).resolve()
    if not document.is_relative_to(root.resolve()) or not document.is_file() or evidence == state.get('owner_statement'):
        raise RuntimeError('QIP publication blocked: owner assertion is not permission evidence')
    if not state.get('licensor') or not state.get('scope') or not state.get('reviewed_by'):
        raise RuntimeError('QIP publication blocked: permission review details missing')
    if hashlib.sha256(document.read_bytes()).hexdigest() != state.get('evidence_sha256'):
        raise RuntimeError('QIP publication blocked: permission evidence changed')


def require_publication(root: Path = ROOT) -> None:
    require_permission(root)
    audit = json.loads((root / 'docs/release-audit.json').read_text(encoding='utf-8'))
    if audit.get('new_public_release_approved') is not True:
        raise RuntimeError('New public release blocked: audit/release approval is missing')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publication', action='store_true')
    args = parser.parse_args()
    try:
        (require_publication if args.publication else require_permission)()
    except RuntimeError as error:
        parser.exit(1, str(error) + '\n')
