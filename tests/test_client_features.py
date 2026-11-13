from __future__ import annotations
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "build/tests/native-harness.exe"
CLIENT = ROOT / "build/tests/qip.exe"


class ClientFeatureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qip-client-feature-")
        self.dir = Path(self.temp.name)
        self.app = self.dir / "QIP2005"
        self.users = self.app / "Users/demo"
        self.history = self.users / "History"
        self.history.mkdir(parents=True)
        self.message = self.history / "example.txt"
        self.message.write_bytes(b"synthetic old history\r\n")
        self.config = self.users / "profile.ini"
        self.old_config = (
            "[General]\r\nStartup=1\r\nHistPath=Users\\demo\\History\r\n"
            "SaveHistMsg=Yes\r\nHisSaveNIL=1\r\nUnrelated=unchanged\r\n"
        )
        self.config.write_bytes(self.old_config.encode("cp1251"))
        (self.app / "LI").mkdir()
        (self.app / "LI/current.cfg").write_bytes(b"Russian\r\n204\r\n")
        shutil.copy2(CLIENT, self.app / "qip.exe")
        (self.app / "keep-running").touch()
        (self.app / "hidden-fixture").touch()
        self.storage = self.dir / "copies"
        self.fixture = {"app": str(self.app), "storage": str(self.storage), "timestamp": 1000}
        self.clients = [self.app]
        self.counter = 0
        self.addCleanup(self.cleanup)

    def cleanup(self):
        for app in self.clients:
            if app.exists():
                (app / "stop-fixture").touch()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if all(self.native("client-state", app=str(app)).get("instances", 0) == 0 for app in self.clients):
                break
            time.sleep(0.05)
        self.temp.cleanup()

    def fixture_file(self, **changes):
        fixture = self.dir / f"fixture-{self.counter}.json"
        self.counter += 1
        fixture.write_text(json.dumps(self.fixture | changes), encoding="utf-8")
        return fixture

    def native(self, mode, **changes):
        fixture = self.fixture_file(**changes)
        result = subprocess.run([str(HARNESS), mode, str(fixture)], capture_output=True, text=True, check=True, timeout=30)
        return json.loads(result.stdout)

    def wait_for(self, predicate):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.05)
        self.fail("Controlled fixture did not reach expected state")

    def backup(self, **changes):
        return self.native("profile-backup", **changes)

    def manifest(self, backup):
        return self.storage / backup["id"] / "profile.json"

    def test_repeated_launch_keeps_one_client(self):
        first = self.native("launch")
        second = self.native("launch")
        self.assertTrue(first["started"])
        self.assertFalse(second["started"])
        self.assertEqual(first["pid"], second["pid"])
        self.assertEqual(second["instances"], 1)
        self.assertEqual((self.app / "starts.txt").read_text().splitlines(), ["start"])

    def test_simultaneous_launches_start_exactly_one_client(self):
        fixture = self.fixture_file(show=False)
        def run(_):
            return json.loads(subprocess.run([str(HARNESS), "launch", str(fixture)], capture_output=True, text=True, check=True, timeout=15).stdout)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            results = list(executor.map(run, range(8)))
        self.assertFalse(any("error" in value for value in results))
        self.assertEqual(sum(value["started"] for value in results), 1)
        self.assertEqual(self.native("client-state")["instances"], 1)

    def test_hidden_client_is_shown_by_second_launch(self):
        self.native("launch", show=False)
        self.wait_for(lambda: (self.app / "starts.txt").exists())
        self.assertFalse(self.native("client-state")["window_visible"])
        result = self.native("launch")
        self.assertFalse(result["started"])
        self.assertTrue(result["window_shown"])
        self.wait_for(lambda: self.native("client-state")["window_visible"])

    def test_minimized_client_is_restored(self):
        (self.app / "hidden-fixture").unlink()
        (self.app / "minimized-fixture").touch()
        self.native("launch", show=False)
        self.wait_for(lambda: self.native("client-state").get("window_minimized", False))
        self.native("launch")
        self.wait_for(lambda: not self.native("client-state").get("window_minimized", True))
        self.assertEqual(self.native("client-state")["instances"], 1)

    def test_delphi_owned_hidden_window_is_shown(self):
        (self.app / "vcl-owner").touch()
        self.native("launch", show=False)
        self.wait_for(lambda: (self.app / "starts.txt").exists())
        self.assertFalse(self.native("client-state")["window_visible"])
        self.assertTrue(self.native("launch")["window_shown"])
        self.wait_for(lambda: self.native("client-state")["window_visible"])

    def test_new_client_uses_application_directory(self):
        self.native("launch")
        self.wait_for(lambda: (self.app / "working-directory.bin").exists())
        self.assertEqual(Path((self.app / "working-directory.bin").read_bytes().decode("utf-16le")), self.app)

    def test_different_installation_has_its_own_client(self):
        other = self.dir / "another/QIP2005"
        shutil.copytree(self.app, other)
        self.clients.append(other)
        self.assertTrue(self.native("launch", show=False)["started"])
        self.assertTrue(self.native("launch", app=str(other), show=False)["started"])
        self.assertEqual(self.native("client-state")["instances"], 1)
        self.assertEqual(self.native("client-state", app=str(other))["instances"], 1)

    def test_profile_migration_only_changes_history_and_native_startup(self):
        result = self.native("prepare-profile")
        self.assertTrue(result["prepared"])
        expected = self.old_config.replace("Startup=1", "Startup=0").replace("Users\\demo\\History", str(self.history))
        self.assertEqual(self.config.read_bytes(), expected.encode("cp1251"))
        self.assertEqual(self.message.read_bytes(), b"synthetic old history\r\n")
        self.assertTrue(list((self.dir / "agent-state/profile-migration").rglob("original.ini")))

    def test_profile_migration_preserves_supported_encodings(self):
        for encoding in ("utf-8-sig", "utf-16", "cp1251"):
            with self.subTest(encoding=encoding):
                text = self.old_config + "Caption=Проверка\r\n"
                self.config.write_bytes(text.encode(encoding))
                self.assertTrue(self.native("prepare-profile")["prepared"])
                expected = text.replace("Startup=1", "Startup=0").replace("Users\\demo\\History", str(self.history))
                self.assertEqual(self.config.read_bytes(), expected.encode(encoding))

    def test_profile_is_not_changed_while_client_runs(self):
        before = self.config.read_bytes()
        self.native("launch", show=False)
        self.assertEqual(self.native("prepare-profile")["error"], "application_busy")
        self.assertEqual(self.config.read_bytes(), before)

    def test_first_automatic_backup_contains_exact_history_and_settings(self):
        result = self.backup()
        self.assertEqual(result["status"], "created")
        manifest = json.loads(self.manifest(result).read_text())
        payload = self.storage / result["id"] / "content/users"
        self.assertEqual((payload / "demo/History/example.txt").read_bytes(), self.message.read_bytes())
        self.assertEqual((payload / "demo/profile.ini").read_bytes(), self.config.read_bytes())
        self.assertEqual(manifest["reason"], "after-exit")
        self.assertFalse(any(path.name.endswith(".exe") for path in payload.rglob("*")))

    def test_automatic_backup_is_limited_to_once_per_day(self):
        first = self.backup()
        self.message.write_bytes(b"synthetic changed history")
        self.assertEqual(self.backup(timestamp=1100)["status"], "not_due")
        second = self.backup(timestamp=87401)
        self.assertEqual(second["status"], "created")
        self.assertNotEqual(first["id"], second["id"])
        self.assertEqual(second["count"], 2)

    def test_unchanged_data_does_not_make_another_automatic_copy(self):
        self.backup()
        self.assertEqual(self.backup(timestamp=87401)["status"], "unchanged")
        self.assertEqual(self.native("profile-list")["count"], 1)

    def test_disabled_automatic_backup_makes_no_copy(self):
        result = self.backup(enabled=False)
        self.assertEqual(result["status"], "disabled")
        self.assertEqual(result["count"], 0)

    def test_manual_backup_works_even_when_automatic_backup_is_disabled(self):
        self.backup(manual=True, enabled=False)
        result = self.backup(manual=True, enabled=False, timestamp=1001)
        self.assertEqual(result["status"], "created")
        self.assertEqual(result["count"], 2)

    def test_backup_does_not_copy_live_profile(self):
        self.native("launch", show=False)
        self.assertEqual(self.backup(manual=True)["error"], "application_busy")
        self.assertFalse(self.storage.exists())

    def test_external_history_configured_in_client_is_included(self):
        extra = self.dir / "external-history"
        extra.mkdir()
        (extra / "sample.txt").write_bytes(b"external synthetic history")
        self.config.write_bytes(self.old_config.replace("Users\\demo\\History", str(extra)).encode("cp1251"))
        result = self.backup(manual=True)
        manifest = json.loads(self.manifest(result).read_text())
        self.assertEqual(len([source for source in manifest["sources"] if source["id"].startswith("history-")]), 1)

    def test_recursive_external_history_is_rejected(self):
        self.config.write_bytes(self.old_config.replace("Users\\demo\\History", str(self.dir)).encode("cp1251"))
        self.assertEqual(self.backup(manual=True)["error"], "history_recursive")

    def test_backup_can_restore_deleted_profile_files(self):
        result = self.backup(manual=True)
        shutil.rmtree(self.app / "Users")
        restored = self.native("profile-restore", id=result["id"])
        self.assertTrue(restored["restored"])
        self.assertEqual(self.message.read_bytes(), b"synthetic old history\r\n")

    def test_restore_makes_safety_backup_and_keeps_unrelated_new_files(self):
        first = self.backup(manual=True)
        self.message.write_bytes(b"newer synthetic history")
        new = self.history / "another.txt"
        new.write_bytes(b"new contact")
        restored = self.native("profile-restore", id=first["id"])
        self.assertTrue(restored["restored"])
        self.assertEqual(restored["count"], 2)
        self.assertEqual(self.message.read_bytes(), b"synthetic old history\r\n")
        self.assertEqual(new.read_bytes(), b"new contact")
        manifests = [json.loads(path.read_text()) for path in self.storage.glob("profile-*/profile.json")]
        safety = next(item for item in manifests if item["reason"] == "before-restore")
        self.assertEqual((self.storage / safety["id"] / "content/users/demo/History/example.txt").read_bytes(), b"newer synthetic history")

    def test_corrupt_backup_is_rejected_before_current_files_are_changed(self):
        first = self.backup(manual=True)
        (self.storage / first["id"] / "content/users/demo/History/example.txt").write_bytes(b"damaged")
        before = self.message.read_bytes()
        self.assertEqual(self.native("profile-restore", id=first["id"])["error"], "profile_backup_corrupt")
        self.assertEqual(self.message.read_bytes(), before)
        self.assertEqual(self.native("profile-list")["count"], 1)

    def test_traversal_in_backup_manifest_is_rejected(self):
        first = self.backup(manual=True)
        path = self.manifest(first)
        manifest = json.loads(path.read_text())
        manifest["sources"][0]["files"][0]["path"] = "../outside.txt"
        path.write_text(json.dumps(manifest))
        self.assertEqual(self.native("profile-restore", id=first["id"])["error"], "profile_backup_path")
        self.assertFalse((self.dir / "outside.txt").exists())

    def test_backup_of_another_installation_is_not_restored(self):
        first = self.backup(manual=True)
        other = self.dir / "another/QIP2005"
        shutil.copytree(self.app, other)
        self.clients.append(other)
        self.assertEqual(self.native("profile-restore", app=str(other), id=first["id"])["error"], "profile_backup_owner")

    def test_interrupted_restore_is_recovered_from_safety_copy(self):
        first = self.backup(manual=True)
        self.message.write_bytes(b"simulated interrupted restore")
        name = "restore-" + hashlib.sha256(str(self.app).encode()).hexdigest()[:24] + ".json"
        pending = self.storage / name
        pending.write_text(json.dumps({"app": str(self.app), "previous": first["id"]}))
        self.assertTrue(self.native("profile-recover")["recovered"])
        self.assertEqual(self.message.read_bytes(), b"synthetic old history\r\n")
        self.assertFalse(pending.exists())

    def test_production_monitor_waits_for_client_exit_then_backs_up(self):
        self.native("launch", show=False)
        state = self.dir / "agent-state"
        state.mkdir(exist_ok=True)
        (state / "settings.json").write_text(json.dumps({"check_automatically": False, "backup_after_exit": True}))
        fixture = self.fixture_file()
        monitor = subprocess.Popen([str(HARNESS), "session-monitor", str(fixture)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(0.2)
        self.assertIsNone(monitor.poll())
        self.assertFalse(list((state / "profile-backups").glob("profile-*/profile.json")))
        (self.app / "stop-fixture").touch()
        stdout, stderr = monitor.communicate(timeout=10)
        self.assertEqual(monitor.returncode, 0, stderr)
        self.assertTrue(json.loads(stdout)["finished"])
        self.assertEqual(len(list((state / "profile-backups").glob("profile-*/profile.json"))), 1)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ClientFeatureTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (ROOT / "build/tests/client-feature-results.json").write_text(json.dumps({
        "tests": result.testsRun, "failures": len(result.failures),
        "errors": len(result.errors), "skipped": len(result.skipped),
        "uses_only_controlled_client_fixture": True,
    }, indent=2) + "\n")
    raise SystemExit(not result.wasSuccessful())
