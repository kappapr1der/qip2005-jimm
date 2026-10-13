"""Explicit maintainer action: send the local protected key to a GitHub environment secret."""
import argparse, base64, json, subprocess
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from sign_release import load_key, ROOT

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--key-dpapi',required=True,type=Path)
    p.add_argument('--environment',required=True,choices=['qip-release','qip-private-build'])
    a=p.parse_args()
    key=load_key(a.key_dpapi)
    secret=base64.b64encode(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
    repo=json.loads((ROOT/'repository.json').read_text())
    # No private value in arguments, console output or temporary plaintext files.
    subprocess.run(['gh','secret','set','MANIFEST_SIGNING_KEY_B64','--repo',repo['owner']+'/'+repo['name'],'--env',a.environment],input=secret,check=True)
    print('Signing secret sent to the explicitly selected GitHub environment.')
