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


def require_publication(root: Path = ROOT, owner_decision: Path | None = None) -> None:
    audit = json.loads((root / 'docs/release-audit.json').read_text(encoding='utf-8'))
    version = (root / 'VERSION').read_text(encoding='utf-8').strip()
    if audit.get('new_public_release_approved') is not True or audit.get('version') != version:
        raise RuntimeError('New public release blocked: audit/release approval is missing')
    if owner_decision is None:
        require_permission(root)
        return
    # Explicit local publication instruction, separate from independently verified rights.
    record = audit.get('decision_record')
    if not isinstance(record, str) or Path(record).is_absolute() or '..' in Path(record).parts:
        raise RuntimeError('Invalid version-scoped publication instruction')
    expected = (root / record).resolve()
    if (not expected.is_relative_to(root.resolve()) or owner_decision.resolve() != expected or
            not expected.is_file() or hashlib.sha256(expected.read_bytes()).hexdigest() != audit.get('decision_sha256')):
        raise RuntimeError('Publication instruction does not match the reviewed record')
    decision = json.loads(expected.read_text(encoding='utf-8'))
    repository = json.loads((root / 'repository.json').read_text(encoding='utf-8'))
    if (decision.get('version') != version or decision.get('project_owner') != repository['owner'] or
            decision.get('action') != 'publish-existing-qip-build' or
            decision.get('explicit_publication_requested') is not True or not decision.get('statement') or
            decision.get('independent_rightsholder_permission_verified') is not False or
            decision.get('source_package_sha256') != '0bcc0f2b0914f89af68200fd811e8c96b9f28fcbdac8ff034bdf02a81fd13a0c'):
        raise RuntimeError('Publication instruction is incomplete or belongs to another version')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publication', action='store_true')
    parser.add_argument('--owner-decision', type=Path)
    args = parser.parse_args()
    try:
        if args.owner_decision and not args.publication:
            raise RuntimeError('Owner instruction does not establish rightsholder permission')
        if args.publication:
            require_publication(owner_decision=args.owner_decision)
        else:
            require_permission()
    except RuntimeError as error:
        parser.exit(1, str(error) + '\n')
