"""Fail before publishing if the source tree contains client files or secrets."""
from __future__ import annotations
import argparse, json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def check(git: str):
    tracked=subprocess.check_output([git,'-C',str(ROOT),'ls-files','-z'])
    new=subprocess.check_output([git,'-C',str(ROOT),'ls-files','--others','--exclude-standard','-z'])
    files=set(filter(None,(tracked+new).decode('utf-8').split('\0')))
    forbidden={'.exe','.dll','.7z','.zip','.wav','.bmp','.gif','.ico','.pfx','.dpapi','.lnk','.log'}
    for name in files:
        path=Path(name)
        if path.suffix.lower() in forbidden or any(part.lower() in {'users','history','private','backups','downloads'} for part in path.parts):
            raise RuntimeError('Excluded files entered the source index')
        raw=(ROOT/path).read_bytes()
        if re.search(rb'(?m)^-----BEGIN (?:RSA )?PRIVATE KEY-----\r?$',raw) or re.search(rb'gh[pousr]_[A-Za-z0-9]{30,}',raw):
            raise RuntimeError('Secret material entered the source tree')
    version=(ROOT/'VERSION').read_text().strip()
    assert re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+',version)
    script=(ROOT/'installer/QIP2005.iss').read_text(encoding='utf-8-sig')
    assert 'AppId={{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}' in script
    assert 'DefaultDirName={localappdata}\\Programs\\QIP2005' in script and 'PrivilegesRequired=lowest' in script
    assert 'VersionInfoVersion={#PackageVersion}.0' in script
    assert f'#define PackageVersion "{version}"' in script
    resource=(ROOT/'updater/src/agent.rc').read_text(encoding='utf-8')
    assert 'FILEVERSION '+version.replace('.',',')+',0' in resource
    assert 'VALUE "FileVersion", "'+version+'"' in resource
    assert 'CloseApplications=no' in script and '[UninstallRun]' not in script
    assert script.count('onlyifdoesntexist uninsneveruninstall')==5
    state=json.loads((ROOT/'docs/redistribution.json').read_text())
    if state['qip_redistribution_approved']:
        evidence=state.get('evidence')
        if not evidence or '..' in Path(evidence).parts or not (ROOT/evidence).is_file():
            raise RuntimeError('Written redistribution evidence is required')
    import yaml
    for path in (ROOT/'.github/workflows').glob('*.yml'):
        workflow=yaml.load(path.read_text(encoding='utf-8'),Loader=yaml.BaseLoader)
        assert workflow['permissions']['contents']=='read' and 'jobs' in workflow and 'on' in workflow
        assert 'pull_request_target' not in workflow['on']
    print(f'Source tree checked: {len(files)} files, no original client binaries or secrets.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--git',default='git');check(p.parse_args().git)
