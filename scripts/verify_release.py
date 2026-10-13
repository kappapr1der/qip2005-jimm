from __future__ import annotations
import argparse, base64, hashlib, json, re, time
from pathlib import Path
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import padding
ROOT=Path(__file__).resolve().parents[1]

def verify(directory: Path):
    data=(directory/'update-manifest.json').read_bytes()
    signature=base64.b64decode((directory/'update-manifest.sig').read_text().strip(),validate=True)
    public=serialization.load_pem_public_key((ROOT/'updater/keys/release-public.pem').read_bytes())
    public.verify(signature,data,padding.PKCS1v15(),hashes.SHA256())
    manifest=json.loads(data);version=(ROOT/'VERSION').read_text().strip()
    repo=json.loads((ROOT/'repository.json').read_text())
    assert manifest['repository']==repo['owner']+'/'+repo['name'] and manifest['schema']==1
    assert manifest['version']==version and manifest['tag']=='v'+version
    assert manifest['asset']==f'QIP2005-Jimm-Setup-{version}.exe'
    assert manifest['issued_at']<=int(time.time())+300<=manifest['expires_at']+300
    installer=directory/manifest['asset']
    with installer.open('rb') as file: digest=hashlib.file_digest(file,'sha256').hexdigest()
    assert digest==manifest['sha256'] and installer.stat().st_size==manifest['size']
    assert len(manifest['files'])==155
    for entry in manifest['files']:
        assert '..' not in entry['path'].split('/') and ':' not in entry['path'] and '\\' not in entry['path']
        assert not entry['path'].lower().startswith('users/')
        assert re.fullmatch('[0-9a-f]{64}',entry['sha256'])
    for row in (directory/'SHA256SUMS.txt').read_text().splitlines():
        expected,name=row.split('  ',1)
        assert Path(name).name==name
        with (directory/name).open('rb') as file: assert hashlib.file_digest(file,'sha256').hexdigest()==expected
    print('RSA signature, installer SHA-256, version, file list and checksum document verified.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);verify(p.parse_args().directory)
