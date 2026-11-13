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
        (self.root / 'VERSION').write_text('1.1.2')
        (self.root / 'repository.json').write_text('{"owner":"fixture-owner"}')
        self.evidence = self.root / 'docs/fixture-permission.md'
        self.evidence.write_text('Synthetic gate fixture only. Grants no QIP rights.')
        self.state = {'qip_redistribution_approved': True, 'verification_status': 'rightsholder_permission_reviewed',
                      'evidence': 'docs/fixture-permission.md', 'owner_statement': 'docs/owner-assertion.md',
                      'evidence_sha256': hashlib.sha256(self.evidence.read_bytes()).hexdigest(),
                      'licensor': 'Synthetic test rightsholder', 'scope': 'fixture only', 'reviewed_by': 'test'}
        self.save()
        (self.root / 'docs/release-audit.json').write_text('{"version":"1.1.2","new_public_release_approved":false}')

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
        (self.root / 'docs/release-audit.json').write_text('{"version":"1.1.2","new_public_release_approved":true}')
        require_publication(self.root)

    def owner_instruction(self):
        self.state['qip_redistribution_approved'] = False
        self.state['verification_status'] = 'owner_assertion_only'
        self.save()
        decision = self.root / 'docs/fixture-decision.json'
        decision.write_text(json.dumps({'version': '1.1.2', 'project_owner': 'fixture-owner',
            'action': 'publish-existing-qip-build', 'explicit_publication_requested': True,
            'statement': 'Synthetic publication instruction only; grants no rights.',
            'independent_rightsholder_permission_verified': False,
            'source_package_sha256': '0bcc0f2b0914f89af68200fd811e8c96b9f28fcbdac8ff034bdf02a81fd13a0c'}))
        (self.root / 'docs/release-audit.json').write_text(json.dumps({'version': '1.1.2',
            'new_public_release_approved': True, 'decision_record': 'docs/fixture-decision.json',
            'decision_sha256': hashlib.sha256(decision.read_bytes()).hexdigest()}))
        return decision

    def test_owner_instruction_is_explicit_and_does_not_verify_rights(self):
        decision = self.owner_instruction()
        require_publication(self.root, decision)
        with self.assertRaises(RuntimeError): require_permission(self.root)
        with self.assertRaises(RuntimeError): require_publication(self.root)

    def test_changed_publication_instruction_is_rejected(self):
        decision = self.owner_instruction(); decision.write_text('{}')
        with self.assertRaises(RuntimeError): require_publication(self.root, decision)

    def test_instruction_cannot_authorize_another_version(self):
        decision = self.owner_instruction(); (self.root / 'VERSION').write_text('1.1.3')
        with self.assertRaises(RuntimeError): require_publication(self.root, decision)

    def test_unrelated_instruction_path_is_rejected(self):
        decision = self.owner_instruction()
        other = self.root / 'docs/other.json'; other.write_bytes(decision.read_bytes())
        with self.assertRaises(RuntimeError): require_publication(self.root, other)

    def test_wrong_owner_or_payload_is_rejected(self):
        decision = self.owner_instruction()
        content = json.loads(decision.read_text()); content['project_owner'] = 'another-owner'
        decision.write_text(json.dumps(content))
        audit = json.loads((self.root / 'docs/release-audit.json').read_text())
        audit['decision_sha256'] = hashlib.sha256(decision.read_bytes()).hexdigest()
        (self.root / 'docs/release-audit.json').write_text(json.dumps(audit))
        with self.assertRaises(RuntimeError): require_publication(self.root, decision)


if __name__ == '__main__':
    unittest.main()
