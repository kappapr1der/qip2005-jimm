"""Reference byte changes allowed during the real installer integration test."""
from pathlib import Path
import re
import sys

users, app = Path(sys.argv[1]), Path(sys.argv[2])
for path in users.rglob("*"):
    if not path.is_file() or path.suffix.lower() not in {".ini", ".cfg"}:
        continue
    raw = path.read_bytes()
    match = re.search(rb"(?mi)^HistPath=([^\r\n]*)", raw)
    if not match:
        continue
    old = Path(match[1].decode("cp1251"))
    target = old if old.is_absolute() else (app / old).resolve()
    raw = re.sub(rb"(?mi)^(HistPath=)[^\r\n]*", lambda row: row[1] + str(target).encode("cp1251"), raw)
    raw = re.sub(rb"(?mi)^(Startup=)[^\r\n]*", rb"\g<1>0", raw)
    path.write_bytes(raw)
