"""Real worker processes + real Inno installers; synthetic client/data and ephemeral RSA key only."""
from __future__ import annotations
import argparse
import base64
import ctypes
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
import uuid
import winreg
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import hashes

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION_ID = '{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}'
REPO = 'kappapr1der/qip2005-jimm'
BASELINE = '1.1.1'
TARGET = (ROOT / 'VERSION').read_text().strip()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inventory(root):
    return {str(p.relative_to(root)): sha(p) for p in root.rglob('*') if p.is_file()}


def wait_until(predicate, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.1)
    raise RuntimeError('E2E deadline exceeded')


class FixtureServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        owner = self
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                owner.requests.append(self.path)
                status, data = owner.responses.get(self.path, (404, b''))
                self.send_response(status)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, *_):
                pass
        super().__init__(('127.0.0.1', 0), Handler)
        self.responses = {}
        self.requests = []


class Audit:
    def __init__(self, args):
        self.args = args
        self.work = args.work.resolve()
        if self.work.exists():
            raise RuntimeError('E2E work directory already exists; refusing to overwrite')
        self.work.mkdir(parents=True)
        self.id = '{' + str(uuid.uuid4()).upper() + '}'
        self.registration = 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' + self.id + '_is1'
        self.results = []
        self.key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        self.server = FixtureServer()
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.environment = os.environ | {'PATH': str(args.toolchain) + os.pathsep + os.environ['PATH'],
                                         'QIP_E2E_PORT': str(self.server.server_port)}
        self.compile()

    def check(self, value, name):
        self.results.append({'check': name, 'passed': bool(value)})
        print(('PASS: ' if value else 'FAIL: ') + name, flush=True)
        if not value:
            raise RuntimeError(name)

    def run(self, command, **kwargs):
        result = subprocess.run([str(x) for x in command], env=self.environment,
                                capture_output=True, timeout=180, **kwargs)
        if result.returncode:
            (self.work / 'last-command-error.txt').write_bytes(result.stdout + result.stderr)
            raise RuntimeError('E2E command failed; inspect last-command-error.txt in the test work directory')
        return result

    def compile(self):
        tree = self.work / 'source'
        shutil.copytree(ROOT / 'updater', tree / 'updater')
        (tree / 'tests').mkdir()
        shutil.copy2(ROOT / 'tests/e2e_driver.cpp', tree / 'tests/e2e_driver.cpp')
        tool = self.args.toolchain / 'g++.exe'
        self.run([tool, '-Os', '-static', '-municode', '-mwindows', ROOT / 'tests/slow_client.cpp', '-o', self.work / 'qip.exe'])
        (self.work / 'docking.dll').write_bytes(b'inert fixture, never loaded as a library')
        values = {'Repository': REPO, 'PublicModulus': format(self.key.public_key().public_numbers().n, '0768x'),
                  'QipHash': sha(self.work / 'qip.exe'), 'DockingHash': sha(self.work / 'docking.dll')}
        (tree / 'updater/src/generated_config.hpp').write_text('#pragma once\nnamespace qip {\n' +
            ''.join(f'inline constexpr const char* {k} = "{v}";\n' for k, v in values.items()) + '}\n')
        transaction = tree / 'updater/src/transaction.hpp'
        text = transaction.read_text(encoding='utf-8').replace(PRODUCTION_ID, self.id)
        text = text.replace('QIP 2005 (Jimm.online)', 'QIP isolated E2E ' + self.id)
        transaction.write_text(text, encoding='utf-8')
        self.driver = self.work / 'e2e-driver.exe'
        self.run([tool, '-std=c++20', '-Os', '-DQIP_AGENT_TESTING', '-D_WIN32_WINNT=0x0A00', '-DUNICODE', '-D_UNICODE',
                  '-static', '-municode', tree / 'tests/e2e_driver.cpp', '-o', self.driver,
                  '-lwinhttp', '-lbcrypt', '-lcrypt32', '-lshell32', '-lole32', '-ladvapi32', '-luser32', '-lgdi32'])
        shutil.copy2(ROOT / 'tests/fixtures/v1.1.1/main.cpp', tree / 'updater/src/legacy_main.cpp')
        self.legacy_driver = self.work / 'legacy-e2e-driver.exe'
        self.run([tool, '-std=c++20', '-Os', '-DQIP_AGENT_TESTING', '-DQIP_E2E_LEGACY_WORKER', '-D_WIN32_WINNT=0x0A00', '-DUNICODE', '-D_UNICODE',
                  '-static', '-municode', tree / 'tests/e2e_driver.cpp', '-o', self.legacy_driver,
                  '-lwinhttp', '-lbcrypt', '-lcrypt32', '-lshell32', '-lole32', '-ladvapi32', '-luser32', '-lgdi32'])
        for version in (BASELINE, TARGET):
            (self.work / f'version-{version}.json').write_text(json.dumps({'version': version, 'repository': REPO}))
        self.setups = {(BASELINE, False): self.build(BASELINE), (TARGET, False): self.build(TARGET),
                       (TARGET, True): self.build(TARGET, True)}

    def build(self, version, fail=False):
        output = self.work / ('failure-artifact' if fail else 'artifacts-' + version)
        output.mkdir()
        script = output / 'installer.iss'
        text = f'''[Setup]
AppId={{{self.id}
AppName=QIP synthetic E2E fixture
AppVersion={version}
DefaultDirName={{localappdata}}\\QIP-E2E-NOT-FOR-USE\\QIP2005
PrivilegesRequired=lowest
Uninstallable=yes
CloseApplications=no
RestartApplications=no
CreateAppDir=yes
OutputDir={output}
OutputBaseFilename=QIP2005-Jimm-Setup-{version}
[Files]
Source: "{self.work / 'qip.exe'}"; DestDir: "{{app}}"; Flags: ignoreversion
Source: "{self.work / 'docking.dll'}"; DestDir: "{{app}}\\Plugins"; Flags: ignoreversion
Source: "{self.driver}"; DestDir: "{{app}}"; DestName: "QipUpdateAgent.exe"; Flags: ignoreversion
Source: "{self.driver}"; DestDir: "{{app}}"; DestName: "QipLauncher.exe"; Flags: ignoreversion
Source: "{self.work / ('version-' + version + '.json')}"; DestDir: "{{app}}"; DestName: "installed-version.json"; Flags: ignoreversion
'''
        if fail:
            text += f'''[Code]
#include "{ROOT / 'installer/profile-finalization.iss'}"
'''
            text += '''
procedure ExitProcess(Code: Integer);
 external 'ExitProcess@kernel32.dll stdcall';
procedure CurStepChanged(CurStep: TSetupStep);
begin
 if CurStep=ssPostInstall then begin
  FinalizeInstalledProfile;
  SaveStringToFile(ExpandConstant('{app}\\Users\\fixture\\History\\messages.txt'), 'deliberate fixture damage', False);
  ExitProcess(42);
 end;
end;
'''
        else:
            text += f'''[Code]
#include "{ROOT / 'installer/profile-finalization.iss'}"
procedure CurStepChanged(CurStep: TSetupStep);
begin
 if CurStep=ssPostInstall then FinalizeInstalledProfile;
end;
'''
        script.write_text(text, encoding='utf-8-sig')
        self.run([self.args.iscc, script])
        return output / f'QIP2005-Jimm-Setup-{version}.exe'

    def fixture(self, name):
        case = self.work / name
        app = case / 'QIP2005'
        self.environment['QIP_E2E_STATE'] = str(case / 'agent-state')
        app.mkdir(parents=True)
        self.run([self.setups[BASELINE, False], '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/DIR=' + str(app)])
        history = app / 'Users/fixture/History'
        history.mkdir(parents=True)
        (history / 'messages.txt').write_bytes('История: синтетические сообщения\r\n'.encode('cp1251') + bytes(range(256)))
        (history.parent / 'profile.ini').write_bytes(('[Main]\r\nStartup=0\r\nHistPath=' + str(history) + '\r\nSaveHistMsg=Yes\r\n').encode('utf-8-sig'))
        state = case / 'agent-state'
        state.mkdir()
        (state / 'settings.json').write_text(json.dumps({'check_automatically': True, 'install_after_exit': True,
                                                        'backup_after_exit': False, 'external_history_directory': ''}))
        return app, state

    def offer(self, fail=False, invalid_signature=False, corrupt_installer=False, tag=None):
        tag = tag or 'v' + TARGET
        setup = self.setups[TARGET, fail]
        files = {'qip.exe': sha(self.work / 'qip.exe'), 'Plugins/docking.dll': sha(self.work / 'docking.dll'),
                 'QipUpdateAgent.exe': sha(self.driver), 'QipLauncher.exe': sha(self.driver),
                 'installed-version.json': sha(self.work / ('version-' + TARGET + '.json'))}
        now = int(time.time())
        manifest = {'schema': 1, 'repository': REPO, 'version': TARGET, 'tag': 'v' + TARGET,
                    'asset': setup.name, 'size': setup.stat().st_size, 'sha256': sha(setup),
                    'minimum_installed_version': '1.0.0', 'issued_at': now, 'expires_at': now + 86400,
                    'files': [{'path': p, 'sha256': h} for p, h in files.items()]}
        raw = json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()
        signature = self.key.sign(raw, padding.PKCS1v15(), hashes.SHA256())
        if invalid_signature:
            signature = bytes([signature[0] ^ 1]) + signature[1:]
        sig = base64.b64encode(signature) + b'\n'
        assets = {'update-manifest.json': raw, 'update-manifest.sig': sig, setup.name: setup.read_bytes()}
        release = {'draft': False, 'prerelease': False, 'tag_name': tag, 'assets': [
            {'name': name, 'size': len(data), 'browser_download_url': f'https://github.com/{REPO}/releases/download/{tag}/{name}'}
            for name, data in assets.items()]}
        self.server.responses = {'/api/latest': (200, json.dumps(release).encode())}
        self.server.responses.update({'/assets/' + name: (200, data) for name, data in assets.items()})
        if corrupt_installer:
            damaged = bytearray(assets[setup.name]); damaged[-1] ^= 1
            self.server.responses['/assets/' + setup.name] = (200, bytes(damaged))
        return raw, sig

    def native(self, mode, app, *args, expected_error=None, legacy=False):
        result = subprocess.run([str(self.legacy_driver if legacy else self.driver), mode, str(app), *map(str, args)], env=self.environment,
                                capture_output=True, timeout=90)
        output = json.loads(result.stdout)
        if expected_error:
            self.check(result.returncode != 0 and output.get('error') == expected_error, expected_error + ' rejected before install')
        elif result.returncode:
            raise RuntimeError('Native E2E failed: ' + output.get('error', 'unknown'))
        return output

    @staticmethod
    def phases(state):
        return [p.read_text() for p in (state / 'transactions').glob('*/phase')]

    def run_cases(self):
        app, state = self.fixture('success')
        self.offer()
        before = inventory(app / 'Users')
        (app / 'keep-running').touch()
        child = subprocess.Popen([str(app / 'qip.exe')], cwd=app, env=self.environment)
        try:
            wait_until(lambda: (app / 'starts.txt').exists())
            self.check(self.native('--cycle', app)['scheduled'], 'signed release downloaded and worker scheduled')
            time.sleep(2.5)
            self.check(child.poll() is None and self.phases(state) == ['prepared'] and
                       json.loads((app / 'installed-version.json').read_text())['version'] == BASELINE,
                       'real worker waits while controlled QIP process is running')
            self.check(inventory(app / 'Users') == before, 'history untouched while update waits')
        finally:
            (app / 'stop-fixture').touch(); child.wait(timeout=10)
        wait_until(lambda: self.phases(state) == ['installed'])
        self.check(json.loads((app / 'installed-version.json').read_text())['version'] == TARGET, f'real Inno upgrade {BASELINE} to {TARGET}')
        self.check(inventory(app / 'Users') == before, 'every synthetic profile and history byte preserved')
        snapshots = list((state / 'backups').glob('*/snapshot.json'))
        self.check(len(snapshots) == 1 and inventory(snapshots[0].parent / 'application/Users') == before, 'complete pre-install snapshot preserved')
        self.check(' application_busy\n' not in (state / 'agent.log').read_text(), 'worker and installer profile finalization have no mutex conflict')
        self.check(not self.native('--cycle', app)['scheduled'], 'equal release version produces no update')
        self.offer(tag='v1.0.0')
        self.check(not self.native('--cycle', app)['scheduled'], 'older release version produces no downgrade')
        self.offer()
        (app / 'stop-fixture').unlink()
        self.run([app / 'QipLauncher.exe'], cwd=app)
        self.run([app / 'QipLauncher.exe'], cwd=app)
        self.check((app / 'starts.txt').read_text().count('start') == 2, 'repeat launch after update starts only one client')
        (app / 'stop-fixture').touch()
        wait_until(lambda: not self.native('--busy', app)['busy'])

        app, state = self.fixture('profile-finalization')
        config = app / 'Users/fixture/profile.ini'
        original = b'[Main]\r\nStartup=1\r\nHistPath=Users\\fixture\\History\r\nSaveHistMsg=Yes\r\n'
        config.write_bytes(original)
        history_before = inventory(app / 'Users/fixture/History')
        self.offer(); self.native('--cycle', app)
        wait_until(lambda: self.phases(state) == ['installed'])
        expected = ('[Main]\r\nStartup=0\r\nHistPath=' + str(app / 'Users/fixture/History') + '\r\nSaveHistMsg=Yes\r\n').encode()
        self.check(config.read_bytes() == expected, 'worker finalizes relative HistPath and Startup under its own install lock')
        self.check(inventory(app / 'Users/fixture/History') == history_before, 'profile finalization preserves history bytes')
        snapshots = list((state / 'backups').glob('*/snapshot.json'))
        self.check((snapshots[0].parent / 'application/Users/fixture/profile.ini').read_bytes() == original,
                   'pre-migration profile remains recoverable in the complete snapshot')

        app, state = self.fixture('legacy-worker-handoff')
        for name in ('QipUpdateAgent.exe', 'QipLauncher.exe'):
            shutil.copy2(self.legacy_driver, app / name)
        config = app / 'Users/fixture/profile.ini'; config.write_bytes(original)
        history_before = inventory(app / 'Users/fixture/History')
        self.offer(); self.native('--cycle', app, legacy=True)
        wait_until(lambda: self.phases(state) == ['installed'])
        expected = ('[Main]\r\nStartup=0\r\nHistPath=' + str(app / 'Users/fixture/History') + '\r\nSaveHistMsg=Yes\r\n').encode()
        self.check(config.read_bytes() == expected and inventory(app / 'Users/fixture/History') == history_before,
                   'published v1.1.1 worker logic accepts validated new-helper handoff without losing history')
        self.check(' application_busy\n' not in (state / 'agent.log').read_text(), 'legacy worker handoff has no install mutex conflict')

        app, state = self.fixture('worker-path-alias')
        for name in ('QipUpdateAgent.exe', 'QipLauncher.exe'):
            shutil.copy2(self.legacy_driver, app / name)
        before = inventory(app / 'Users')
        self.environment['QIP_E2E_STATE'] = '\\\\?\\' + str(state)
        self.offer(); self.native('--cycle', app, legacy=True)
        wait_until(lambda: self.phases(state) == ['installed'])
        self.check(inventory(app / 'Users') == before, 'Windows path alias permits verified old-worker update and preserves history')
        self.check(' worker_path_alias_accepted\n' in (state / 'agent.log').read_text(), 'handoff identifies the actual worker by Windows file ID rather than path spelling')

        app, state = self.fixture('legacy-postinstall-error')
        for name in ('QipUpdateAgent.exe', 'QipLauncher.exe'):
            shutil.copy2(self.legacy_driver, app / name)
        before = inventory(app / 'Users')
        self.environment['QIP_E2E_FAIL_FINALIZE'] = '1'
        try:
            self.offer(); self.native('--cycle', app, legacy=True)
            wait_until(lambda: self.phases(state) == ['rolled-back'])
        finally:
            self.environment.pop('QIP_E2E_FAIL_FINALIZE', None)
        self.check(inventory(app / 'Users') == before and json.loads((app / 'installed-version.json').read_text())['version'] == BASELINE,
                   'post-install helper error returns failure to legacy worker and restores damaged history')

        app, state = self.fixture('failed-install')
        before = inventory(app / 'Users'); prior = inventory(app)
        self.offer(fail=True)
        self.native('--cycle', app)
        wait_until(lambda: self.phases(state) == ['rolled-back'])
        self.check(inventory(app / 'Users') == before and inventory(app) == prior,
                   'failed real Inno installer rolls back application and deliberately damaged history')
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.registration) as key:
            version = winreg.QueryValueEx(key, 'DisplayVersion')[0]
        self.check(version == BASELINE, 'failed installation restores previous Windows registration')

        app, state = self.fixture('recovery')
        before = inventory(app / 'Users')
        raw, sig = self.offer()
        (state / 'test-manifest.json').write_bytes(raw); (state / 'test-manifest.sig').write_bytes(sig)
        self.native('--prime-crash', app, state / 'test-manifest.json', state / 'test-manifest.sig')
        self.check(self.native('--recover', app)['recover_started'], 'pending installation journal starts actual recovery worker')
        wait_until(lambda: self.phases(state) == ['rolled-back'])
        self.check(inventory(app / 'Users') == before and json.loads((app / 'installed-version.json').read_text())['version'] == BASELINE,
                   'interrupted-update recovery restores previous version and history')

        for name, options, error in [('bad-signature', {'invalid_signature': True}, 'signature_invalid'),
                                     ('bad-hash', {'corrupt_installer': True}, 'installer_corrupt')]:
            app, state = self.fixture(name); before = inventory(app)
            self.offer(**options)
            self.native('--cycle', app, expected_error=error)
            self.check(not self.phases(state) and inventory(app) == before, name + ' never schedules installer or changes profiles')
        app, state = self.fixture('github-unavailable'); before = inventory(app)
        self.server.responses = {'/api/latest': (503, b'')}
        self.native('--cycle', app, expected_error='github_unavailable')
        self.check(inventory(app) == before, 'unavailable API preserves existing application')

        app, state = self.fixture('untrusted-finalize-handoff'); before = inventory(app / 'Users')
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
        kernel.CreateMutexW.restype = ctypes.c_void_p
        kernel.ReleaseMutex.argtypes = kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        mutex_name = 'Local\\QIP2005.Install.' + hashlib.sha256(str(app).lower().encode()).hexdigest()[:32]
        handle = kernel.CreateMutexW(None, True, mutex_name)
        if not handle:
            raise RuntimeError('Cannot create isolated test mutex')
        try:
            self.native('--prepare-profile-from-installer', app, expected_error='worker_handoff_required')
        finally:
            kernel.ReleaseMutex(handle); kernel.CloseHandle(handle)
        self.check(inventory(app / 'Users') == before, 'held mutex without authenticated active worker cannot authorize profile writes')

    def close(self):
        self.server.shutdown(); self.server.server_close()
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, self.registration)
        except FileNotFoundError:
            pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--iscc', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    audit = None
    try:
        audit = Audit(args)
        audit.run_cases()
    finally:
        if audit:
            audit.close()
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps({'checks': audit.results, 'test_key_only': True,
                'baseline_version': BASELINE, 'target_version': TARGET,
                'test_build_overrides': ['ephemeral RSA public key', 'isolated AppId and shortcut names', 'test entry point and transport'],
                'real_qip_binary_used': False,
                'source_sha256': {name: sha(ROOT / name) for name in ['updater/src/main.cpp', 'updater/src/transaction.hpp', 'installer/profile-finalization.iss', 'tests/e2e_driver.cpp']},
                'transport': 'loopback HTTP test adapter; production TLS checked separately',
                'real_inno_and_production_worker_logic': True}, indent=2) + '\n')
