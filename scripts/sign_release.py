"""RSA-3072/SHA-256 signing. Private material is never printed or checked in."""
from __future__ import annotations
import argparse, base64, ctypes, hashlib, json, os, time
from pathlib import Path
from ctypes import wintypes
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import rsa, padding

ROOT = Path(__file__).resolve().parents[1]
class Blob(ctypes.Structure):
    _fields_ = [('cbData', wintypes.DWORD), ('pbData', ctypes.POINTER(ctypes.c_ubyte))]

def dpapi(data: bytes, protect: bool) -> bytes:
    if os.name != 'nt':
        raise RuntimeError('DPAPI signing key requires its original Windows user account')
    backing = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    source, result = Blob(len(data), backing), Blob()
    crypt32, kernel32 = ctypes.WinDLL('crypt32', use_last_error=True), ctypes.WinDLL('kernel32')
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    if protect:
        ok = crypt32.CryptProtectData(ctypes.byref(source), 'QIP2005 release signing', None, None, None, 1, ctypes.byref(result))
    else:
        ok = crypt32.CryptUnprotectData(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(result))
    if not ok:
        raise RuntimeError('Windows key protection failed')
    try:
        return ctypes.string_at(result.pbData, result.cbData)
    finally:
        kernel32.LocalFree(result.pbData)

def initialize(path: Path) -> None:
    if path.exists():
        raise RuntimeError('Signing key already exists; refusing to replace it')
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(dpapi(pem, True))
    public = key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
    (ROOT/'updater/keys/release-public.pem').write_bytes(public)
    manifest = json.loads((ROOT/'installer/payload-manifest.json').read_text(encoding='utf-8-sig'))
    files = {e['path'].lower(): e['sha256'] for e in manifest}
    repo = json.loads((ROOT/'repository.json').read_text())
    modulus = format(key.public_key().public_numbers().n, '0768x')
    header = '#pragma once\nnamespace qip {\n'
    values = {'Repository': repo['owner']+'/'+repo['name'], 'PublicModulus':modulus, 'QipHash':files['qip.exe'], 'DockingHash':files['plugins/docking.dll']}
    for name, value in values.items():
        header += f'inline constexpr const char* {name} = "{value}";\n'
    (ROOT/'updater/src/generated_config.hpp').write_text(header+'}\n', encoding='utf-8')
    print('Signing key protected for the current Windows user; public key saved.')

def load_key(path: Path | None):
    if path:
        pem = dpapi(path.read_bytes(), False)
    else:
        secret = os.environ.get('MANIFEST_SIGNING_KEY_B64')
        if not secret:
            raise RuntimeError('Signing key not configured')
        pem = base64.b64decode(secret, validate=True)
    key = serialization.load_pem_private_key(pem, password=None)
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size != 3072:
        raise RuntimeError('Expected RSA-3072 signing key')
    trusted = serialization.load_pem_public_key((ROOT/'updater/keys/release-public.pem').read_bytes())
    if key.public_key().public_numbers() != trusted.public_numbers():
        raise RuntimeError('Signing key does not match the pinned public key')
    return key

def sha(path: Path) -> str:
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()

def sign(directory: Path, agent_directory: Path, payload: Path, key_path: Path | None) -> None:
    version = (ROOT/'VERSION').read_text().strip()
    installer = directory/f'QIP2005-Jimm-Setup-{version}.exe'
    client = json.loads((ROOT/'installer/payload-manifest.json').read_text(encoding='utf-8-sig'))
    defaults = {'Users/connect.cfg','LI/current.cfg','LI/langs.cfg','Skins/current.cfg','Skins/skins.cfg'}
    files = []
    for entry in client:
        if sha(payload/entry['path']) != entry['sha256']:
            raise RuntimeError('Original payload was modified')
        if entry['path'] not in defaults:
            files.append({'path':entry['path'],'sha256':entry['sha256']})
    for name in ['QipUpdateAgent.exe','QipLauncher.exe','installed-version.json']:
        files.append({'path':name,'sha256':sha(agent_directory/name)})
    for file in sorted((agent_directory/'Licenses').glob('*.txt')):
        files.append({'path':'Licenses/'+file.name,'sha256':sha(file)})
    repository=json.loads((ROOT/'repository.json').read_text())
    issued=int(time.time())
    manifest={'schema':1,'repository':repository['owner']+'/'+repository['name'],'version':version,'tag':'v'+version,
              'asset':installer.name,'size':installer.stat().st_size,'sha256':sha(installer),
              'minimum_installed_version':'1.0.0','issued_at':issued,'expires_at':issued+180*86400,'files':files}
    data=json.dumps(manifest, ensure_ascii=True, sort_keys=True, separators=(',',':')).encode()
    signature=load_key(key_path).sign(data,padding.PKCS1v15(),hashes.SHA256())
    (directory/'update-manifest.json').write_bytes(data)
    (directory/'update-manifest.sig').write_text(base64.b64encode(signature).decode()+'\n',encoding='ascii')
    names=[installer.name,'update-manifest.json','update-manifest.sig']
    (directory/'SHA256SUMS.txt').write_text(''.join(f'{sha(directory/n)}  {n}\n' for n in names),encoding='ascii')
    print('Installer hash and RSA-signed manifest prepared locally.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--initialize', type=Path)
    p.add_argument('--key-dpapi', type=Path)
    p.add_argument('--directory', type=Path)
    p.add_argument('--agent-directory', type=Path, default=ROOT/'build/agent')
    p.add_argument('--payload', type=Path, default=ROOT/'private/payload')
    a=p.parse_args()
    if a.initialize:
        initialize(a.initialize)
    elif a.directory:
        sign(a.directory,a.agent_directory,a.payload,a.key_dpapi)
    else:
        p.error('Select --initialize or --directory')
