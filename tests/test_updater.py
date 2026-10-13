from __future__ import annotations
import argparse, base64, copy, hashlib, json, os, shutil, subprocess, tempfile, time, unittest
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa, padding

ROOT=Path(__file__).resolve().parents[1]
HARNESS=ROOT/'build/tests/native-harness.exe'
REAL_BASELINE: Path | None=None
def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

class NativeUpdaterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key=rsa.generate_private_key(public_exponent=65537,key_size=3072)

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='qip-updater-test-')
        self.addCleanup(self.temp.cleanup)
        self.dir=Path(self.temp.name)
        self.app=self.dir/'QIP2005'
        (self.app/'Users'/'example-account'/'History').mkdir(parents=True)
        (self.app/'Users'/'example-account'/'History'/'test.txt').write_bytes(b'private synthetic history')
        (self.app/'Users'/'connect.cfg').write_bytes(b'LoginServer=icq.jimm.online\r\nLoginPort=5190\r\n')
        old={'qip.exe':b'original-client','Plugins/docking.dll':b'original-plugin','installed-version.json':b'{"version":"1.0.0"}',
             'QipUpdateAgent.exe':b'previous-agent','QipLauncher.exe':b'previous-launcher'}
        new={**old,'QipUpdateAgent.exe':b'agent-1.1.0','QipLauncher.exe':b'launcher-1.1.0','installed-version.json':b'{"version":"1.1.0"}'}
        for name,data in old.items():
            file=self.app/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(data)
        self.repository='kappapr1der/qip2005-jimm'
        self.url='https://github.com/'+self.repository+'/releases/download/v1.1.0/'
        self.api='https://api.github.com/repos/'+self.repository+'/releases/latest'
        self.installer=b'synthetic authenticated installer'
        self.manifest={'schema':1,'repository':self.repository,'version':'1.1.0','tag':'v1.1.0','asset':'QIP2005-Jimm-Setup-1.1.0.exe',
            'size':len(self.installer),'sha256':sha(self.installer),'minimum_installed_version':'1.0.0','issued_at':1700000000,
            'expires_at':1700000000+180*86400,'files':[{'path':n,'sha256':sha(d)} for n,d in new.items()]}
        self.release={'tag_name':'v1.1.0','draft':False,'prerelease':False,'assets':[{'name':n,'browser_download_url':self.url+n,'size':len(self.installer) if n.endswith('.exe') else 1000} for n in ['update-manifest.json','update-manifest.sig',self.manifest['asset']]]}
        self.fixture={'repository':self.repository,'modulus':format(self.key.public_key().public_numbers().n,'0768x'),
            'qip_hash':sha(old['qip.exe']),'docking_hash':sha(old['Plugins/docking.dll']),'now':1700001000,'current':'1.0.0',
            'setup':str(self.dir/'setup.exe'),'app':str(self.app),'backup':str(self.dir/'backup'),'phase':str(self.dir/'phase'),
            'consent':True,'installer_bytes':self.installer.decode(),'installed_files':{n:d.decode() for n,d in new.items()}}

    def seal(self):
        data=json.dumps(self.manifest,separators=(',',':'),sort_keys=True).encode()
        signature=base64.b64encode(self.key.sign(data,padding.PKCS1v15(),hashes.SHA256())).decode()
        self.fixture['responses']={self.api:{'status':200,'body':json.dumps(self.release)},self.url+'update-manifest.json':{'status':200,'body':data.decode()},self.url+'update-manifest.sig':{'status':200,'body':signature}}

    def run_native(self,mode='check',seal=True):
        if seal:self.seal()
        fixture=self.dir/'fixture.json';fixture.write_text(json.dumps(self.fixture),encoding='utf-8')
        process=subprocess.run([str(HARNESS),mode,str(fixture)],capture_output=True,text=True,check=True,timeout=30)
        return json.loads(process.stdout)

    def test_valid_update_and_real_rsa_signature(self):
        result=self.run_native();self.assertEqual(result['result'],'approved');self.assertEqual(result['downloads'],1)
    def test_no_new_version(self):
        self.fixture['current']='1.1.0';r=self.run_native();self.assertEqual(r['result'],'no_update');self.assertEqual(r['requests'],1)
    def test_no_releases_404(self):
        self.seal();self.fixture['responses'][self.api]={'status':404,'body':''};self.assertEqual(self.run_native(seal=False)['result'],'no_update')
    def test_github_unavailable(self):
        self.seal();self.fixture['responses'][self.api]={'status':503,'body':''};self.assertEqual(self.run_native(seal=False)['error'],'github_unavailable')
    def test_network_connection_error(self):
        self.fixture['connection_error']=True;self.assertEqual(self.run_native()['error'],'network_error')
    def test_rate_limited(self):
        self.seal();self.fixture['responses'][self.api]={'status':403,'body':''};self.assertEqual(self.run_native(seal=False)['error'],'github_unavailable')
    def test_corrupt_installer(self):
        self.fixture['installer_bytes']='X'+self.installer.decode()[1:];r=self.run_native();self.assertEqual(r['error'],'installer_corrupt');self.assertFalse((self.dir/'backup').exists())
    def test_invalid_signature(self):
        self.seal();self.fixture['responses'][self.url+'update-manifest.sig']['body']=base64.b64encode(b'X'*384).decode();r=self.run_native(seal=False);self.assertEqual(r['error'],'signature_invalid');self.assertFalse((self.dir/'setup.exe').exists())
    def test_forged_manifest_after_signature(self):
        self.seal();self.fixture['responses'][self.url+'update-manifest.json']['body']+=' ';self.assertEqual(self.run_native(seal=False)['error'],'signature_invalid')
    def test_user_declines_after_verified_download(self):
        self.fixture['consent']=False;r=self.run_native('apply');self.assertEqual(r['result'],'declined');self.assertTrue(r['prompted']);self.assertFalse((self.dir/'backup').exists())
    def test_automatic_install_skips_prompt_only_after_opt_in(self):
        self.fixture['automatic']=True;self.fixture['consent']=False;r=self.run_native('apply');self.assertEqual(r['result'],'installed');self.assertFalse(r['prompted'])
    def test_older_release_is_not_installed(self):
        self.fixture['current']='1.2.0';self.assertEqual(self.run_native('apply')['result'],'no_update')
    def test_authenticated_high_watermark_rejects_older_offer(self):
        self.fixture['highest']='1.2.0';self.assertEqual(self.run_native()['error'],'rollback_release')
    def test_busy_qip_never_launches_installer(self):
        self.fixture['busy']=True;r=self.run_native('apply');self.assertEqual(r['result'],'busy');self.assertEqual(r['installer_calls'],0);self.assertTrue(r['user_data_preserved'])
    def test_success_preserves_history(self):
        r=self.run_native('apply');self.assertEqual(r['result'],'installed');self.assertTrue(r['user_data_preserved']);self.assertTrue(r['qip_unchanged']);self.assertTrue(r['backup_exists'])
    def test_existing_updater_is_replaced_from_worker_outside_application(self):
        self.assertNotEqual(HARNESS.parent.resolve(),self.app.resolve())
        r=self.run_native('apply');self.assertEqual(r['result'],'installed')
        self.assertEqual((self.app/'QipUpdateAgent.exe').read_bytes(),b'agent-1.1.0')
        self.assertEqual((self.app/'QipLauncher.exe').read_bytes(),b'launcher-1.1.0')
    def test_native_process_start_does_not_wait_for_client_exit(self):
        slow=HARNESS.parent/'qip.exe';shutil.copy2(slow,self.app/'qip.exe')
        self.fixture={'app':str(self.app),'exe':str(self.app/'qip.exe')}
        result=self.run_native('spawn',seal=False)
        self.assertLess(result['elapsed_ms'],2000);self.assertTrue(result['child_running']);self.assertTrue(result['child_closed'])
    def test_failed_installer_restores_client_history_and_registration(self):
        self.fixture['fail']=True;r=self.run_native('apply');self.assertEqual(r['result'],'rolled_back');self.assertTrue(r['user_data_preserved']);self.assertTrue(r['qip_unchanged']);self.assertEqual(r['integration_state'],1);self.assertEqual(r['current_after'],'1.0.0')
    def test_wrong_installed_file_triggers_rollback(self):
        self.fixture['post_verify_failure']=True;r=self.run_native('apply');self.assertEqual(r['result'],'rolled_back');self.assertTrue(r['user_data_preserved'])
    def test_recursive_backup_is_rejected_before_install(self):
        self.fixture['backup']=str(self.app/'nested-backup');self.assertEqual(self.run_native('apply')['error'],'backup_recursive')
    def test_crash_recovery_from_complete_backup(self):
        r=self.run_native('recover');self.assertTrue(r['recovered'])
    def test_second_start_after_install_finds_current_version(self):
        self.assertEqual(self.run_native('apply')['current_after'],'1.1.0');self.fixture['current']='1.1.0';self.assertEqual(self.run_native()['result'],'no_update')
    def test_checks_are_throttled_for_24_hours(self):
        self.fixture['last']=self.fixture['now']-86399;r=self.run_native();self.assertEqual(r['result'],'throttled');self.assertEqual(r['requests'],0)
    def test_check_is_due_at_24_hours(self):
        self.fixture['last']=self.fixture['now']-86400;self.assertEqual(self.run_native()['result'],'approved')
    def test_checks_can_be_disabled(self):
        self.fixture['enabled']=False;r=self.run_native();self.assertEqual(r['requests'],0)
    def test_manual_check_is_explicit_exception_to_throttle(self):
        self.fixture.update(enabled=False,manual=True);self.assertEqual(self.run_native()['result'],'approved')
    def test_expired_manifest(self):
        self.fixture['now']=self.manifest['expires_at']+1;self.assertEqual(self.run_native()['error'],'manifest_expired')
    def test_future_manifest(self):
        self.manifest['issued_at']=self.fixture['now']+301;self.assertEqual(self.run_native()['error'],'manifest_expired')
    def test_wrong_repository(self):
        self.manifest['repository']='other/repo';self.assertEqual(self.run_native()['error'],'manifest_repository')
    def test_wrong_asset_host(self):
        self.release['assets'][0]['browser_download_url']='https://attacker.invalid/update-manifest.json';self.assertEqual(self.run_native()['error'],'asset_origin')
    def test_duplicate_asset(self):
        self.release['assets'].append(self.release['assets'][0]);self.assertEqual(self.run_native()['error'],'duplicate_asset')
    def test_draft_release(self):
        self.release['draft']=True;self.assertEqual(self.run_native()['error'],'unstable_release')
    def test_path_traversal(self):
        self.manifest['files'].append({'path':'../escape.exe','sha256':'a'*64});self.assertEqual(self.run_native()['error'],'manifest_path')
    def test_nul_in_filename(self):
        self.manifest['files'].append({'path':'file\x00.exe','sha256':'a'*64});self.assertEqual(self.run_native()['error'],'manifest_path')
    def test_manifest_cannot_overwrite_profiles(self):
        self.manifest['files'].append({'path':'Users/Accounts.cfg','sha256':'a'*64});self.assertEqual(self.run_native()['error'],'manifest_path')
    def test_reserved_device_filename(self):
        self.manifest['files'].append({'path':'NUL.txt','sha256':'a'*64});self.assertEqual(self.run_native()['error'],'manifest_path')
    def test_original_client_cannot_be_changed(self):
        self.manifest['files'][0]['sha256']='a'*64;self.assertEqual(self.run_native()['error'],'original_qip_changed')
    def test_original_plugin_cannot_be_changed(self):
        self.manifest['files'][1]['sha256']='a'*64;self.assertEqual(self.run_native()['error'],'original_plugin_changed')
    def test_size_mismatch(self):
        self.release['assets'][-1]['size']+=1;self.assertEqual(self.run_native()['error'],'installer_missing')
    def test_semver_precedence(self):
        for a,b in [('1.2.0','1.10.0'),('1.1.0-alpha.2','1.1.0-alpha.10'),('1.1.0-rc.1','1.1.0')]:
            self.fixture={'a':a,'b':b};self.assertEqual(self.run_native('semver',seal=False)['comparison'],-1)
    def test_semver_build_metadata_does_not_upgrade(self):
        self.fixture={'a':'1.1.0+one','b':'1.1.0+two'};self.assertEqual(self.run_native('semver',seal=False)['comparison'],0)
    def test_real_private_files_survive_success_and_failure(self):
        if REAL_BASELINE is None:self.skipTest('No private real-data baseline supplied')
        shutil.copytree(REAL_BASELINE/'Users',self.app/'Users',dirs_exist_ok=True)
        before={str(p.relative_to(self.app/'Users')):sha(p.read_bytes()) for p in (self.app/'Users').rglob('*') if p.is_file()}
        self.fixture['fail']=True;r=self.run_native('apply');self.assertEqual(r['result'],'rolled_back');self.assertTrue(r['user_data_preserved'])
        self.assertEqual(before,{str(p.relative_to(self.app/'Users')):sha(p.read_bytes()) for p in (self.app/'Users').rglob('*') if p.is_file()})
        self.fixture.update(fail=False,backup=str(self.dir/'second-backup'),phase=str(self.dir/'second-phase'))
        r=self.run_native('apply');self.assertEqual(r['result'],'installed');self.assertTrue(r['user_data_preserved'])
        self.assertEqual(before,{str(p.relative_to(self.app/'Users')):sha(p.read_bytes()) for p in (self.app/'Users').rglob('*') if p.is_file()})

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--real-baseline',type=Path);p.add_argument('--harness',type=Path);p.add_argument('--report',type=Path)
    a=p.parse_args();REAL_BASELINE=a.real_baseline
    if a.harness:HARNESS=a.harness
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(NativeUpdaterTests))
    if a.report:
        a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps({'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'skipped':len(result.skipped),'real_data_test_executed':REAL_BASELINE is not None},indent=2),encoding='utf-8')
    raise SystemExit(not result.wasSuccessful())
