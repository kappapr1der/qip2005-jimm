"""Verify a second DPAPI-protected copy. No network, plaintext key files or secret output."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from sign_release import dpapi, load_key


def restrict_acl(path: Path) -> None:
    # Pass paths through argv, never interpolate them into executable PowerShell text.
    code = r'''$ErrorActionPreference='Stop'
$p=$args[0]
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
$item=Get-Item -LiteralPath $p
if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point refused'}
if($item.PSIsContainer){$acl=[Security.AccessControl.DirectorySecurity]::new()}else{$acl=[Security.AccessControl.FileSecurity]::new()}
$acl.SetAccessRuleProtection($true,$false)
$acl.SetOwner($sid)
foreach($s in @($sid.Value,'S-1-5-18','S-1-5-32-544')){
 $id=[Security.Principal.SecurityIdentifier]::new($s)
 if($item.PSIsContainer){$rule=[Security.AccessControl.FileSystemAccessRule]::new($id,'FullControl','ContainerInherit,ObjectInherit','None','Allow')}
 else{$rule=[Security.AccessControl.FileSystemAccessRule]::new($id,'FullControl','Allow')}
 $acl.AddAccessRule($rule)
}
if($item.PSIsContainer){[IO.Directory]::SetAccessControl($p,$acl);$actual=[IO.Directory]::GetAccessControl($p)}
else{[IO.File]::SetAccessControl($p,$acl);$actual=[IO.File]::GetAccessControl($p)}
if(-not $actual.AreAccessRulesProtected -or $actual.Access.Count -ne 3){throw 'Unexpected key ACL'}
'''
    # -File binds args reliably, including paths with spaces. This script contains no secret.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='qip-key-acl-') as directory:
        script = Path(directory) / 'restrict.ps1'
        script.write_text(code, encoding='utf-8-sig')
        result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(script), str(path)], capture_output=True)
    if result.returncode:
        raise RuntimeError('Could not restrict signing-key ACL: ' + result.stderr.decode(errors='replace')[:1200])


def verify_copy(source: Path, destination: Path) -> dict:
    key = load_key(source)
    copy = load_key(destination)
    public = key.public_key()
    if public.public_numbers() != copy.public_key().public_numbers():
        raise RuntimeError('Backup public key mismatch')
    challenge = b'QIP signing-key recovery audit\0' + os.urandom(32)
    signature = copy.sign(challenge, padding.PKCS1v15(), hashes.SHA256())
    public.verify(signature, challenge, padding.PKCS1v15(), hashes.SHA256())
    damaged = bytearray(destination.read_bytes())
    damaged[-1] ^= 1
    try:
        dpapi(bytes(damaged), False)
    except RuntimeError:
        pass
    else:
        raise RuntimeError('Damaged DPAPI backup unexpectedly decrypted')
    public_der = public.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return {'key_bits': copy.key_size, 'public_key_sha256': hashlib.sha256(public_der).hexdigest(),
            'backup_decrypted_and_signed': True, 'pinned_public_key_matches': True,
            'corrupt_dpapi_rejected': True, 'plaintext_key_written': False,
            'network_used': False, 'local_same_account_backup_only': True,
            'portable_offline_recovery_tested': False}


def backup(source: Path, destination: Path) -> dict:
    source, destination = source.resolve(), destination.resolve()
    if source == destination or destination.exists():
        raise RuntimeError('Refusing to replace an existing key or backup')
    if not source.is_file() or destination.suffix.lower() != '.dpapi':
        raise RuntimeError('Expected a protected source and a new .dpapi backup')
    destination.parent.mkdir(parents=True, exist_ok=True)
    restrict_acl(destination.parent)
    restrict_acl(source)
    with destination.open('xb') as file:
        file.write(source.read_bytes())
    restrict_acl(destination)
    result = verify_copy(source, destination)
    result['acl_restricted_to_owner_system_administrators'] = True
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = backup(args.source, args.destination)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result))
    except Exception:
        print('Signing-key backup failed; no key material is logged.', file=sys.stderr)
        raise SystemExit(1)
