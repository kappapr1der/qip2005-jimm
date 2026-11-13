from __future__ import annotations
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from redistribution_policy import require_permission, require_publication


class PublicationGateTests(unittest.TestCase):
    def setUp(self):
        storage = Path(__file__).resolve().parents[1] / 'build/policy-tests'
        storage.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='qip-rights-fixture-', dir=storage)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'docs').mkdir()
        self.evidence = self.root / 'docs/fixture-permission.md'
        self.evidence.write_text('Synthetic gate fixture only. Grants no QIP rights.')
        self.state = {'qip_redistribution_approved': True, 'verification_status': 'rightsholder_permission_reviewed',
                      'evidence': 'docs/fixture-permission.md', 'owner_statement': 'docs/owner-assertion.md',
                      'evidence_sha256': hashlib.sha256(self.evidence.read_bytes()).hexdigest(),
                      'licensor': 'Synthetic test rightsholder', 'scope': 'fixture only', 'reviewed_by': 'test'}
        self.save()
        (self.root / 'docs/release-audit.json').write_text('{"new_public_release_approved":false}')

    def save(self):
        (self.root / 'docs/redistribution.json').write_text(json.dumps(self.state))

    def test_owner_assertion_cannot_unlock_publication(self):
        self.state['verification_status'] = 'owner_assertion_only'; self.save()
        with self.assertRaises(RuntimeError): require_permission(self.root)

    def test_separate_permission_review_does_not_replace_audit_approval(self):
        require_permission(self.root)
        with self.assertRaises(RuntimeError): require_publication(self.root)

    def test_changed_evidence_requires_new_review(self):
        self.evidence.write_text('changed')
        with self.assertRaises(RuntimeError): require_permission(self.root)

    def test_owner_document_is_not_independent_permission(self):
        self.state['owner_statement'] = self.state['evidence']; self.save()
        with self.assertRaises(RuntimeError): require_permission(self.root)

    def test_paths_outside_repository_are_rejected(self):
        self.state['evidence'] = '../outside.md'; self.save()
        with self.assertRaises(RuntimeError): require_permission(self.root)

    def test_reviewed_fixture_and_explicit_audit_approval_are_required(self):
        (self.root / 'docs/release-audit.json').write_text('{"new_public_release_approved":true}')
        require_publication(self.root)


if __name__ == '__main__':
    unittest.main()
