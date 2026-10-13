from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def verify(payload: Path):
    entries=json.loads((ROOT/'installer/payload-manifest.json').read_text(encoding='utf-8-sig'))
    expected={entry['path'] for entry in entries}
    actual={p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file()}
    if actual!=expected:
        raise RuntimeError('Payload contains missing or extra files; personal files are prohibited')
    expected_directories={parent.as_posix() for entry in entries for parent in Path(entry['path']).parents if str(parent)!='.'}
    actual_directories={p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_dir()}
    if actual_directories!=expected_directories:
        raise RuntimeError('Payload contains extra or missing directories')
    for directory in [payload,*[p for p in payload.rglob('*') if p.is_dir()]]:
        if directory.is_symlink() or getattr(directory,'is_junction',lambda:False)():
            raise RuntimeError('Payload directory links are prohibited')
    for entry in entries:
        file=payload/entry['path']
        if file.is_symlink() or getattr(file,'is_junction',lambda:False)():
            raise RuntimeError('Payload links are prohibited')
        with file.open('rb') as stream:
            digest=hashlib.file_digest(stream,'sha256').hexdigest()
        if digest!=entry['sha256']:
            raise RuntimeError('Original payload hash mismatch')
    print(f'Original payload verified: {len(entries)} files; no extra profile files.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('payload',type=Path);verify(p.parse_args().payload)
