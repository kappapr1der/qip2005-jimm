"""Read-only aggregate disk usage; never emit profile/contact names or message contents."""
from __future__ import annotations
import argparse
import ctypes
import json
import os
from pathlib import Path

class FileStandardInfo(ctypes.Structure):
    _fields_ = [('AllocationSize', ctypes.c_longlong), ('EndOfFile', ctypes.c_longlong),
                ('NumberOfLinks', ctypes.c_ulong), ('DeletePending', ctypes.c_ubyte), ('Directory', ctypes.c_ubyte)]


def usage(root: Path) -> dict:
    result = {'files': 0, 'directories': 0, 'logical_bytes': 0, 'allocated_bytes': 0,
              'skipped_links': 0, 'inaccessible': 0}
    if not root.exists():
        return result
    kernel = ctypes.WinDLL('kernel32', use_last_error=True) if os.name == 'nt' else None
    if kernel:
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.GetFileInformationByHandleEx.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_ulong]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    todo = [root]
    while todo:
        path = todo.pop()
        try:
            info = path.lstat()
            if info.st_file_attributes & 0x400 if os.name == 'nt' else path.is_symlink():
                result['skipped_links'] += 1
                continue
            if path.is_dir():
                result['directories'] += 1
                todo.extend(path.iterdir())
            elif path.is_file():
                result['files'] += 1
                result['logical_bytes'] += info.st_size
                if kernel:
                    handle = kernel.CreateFileW(str(path), 0x80, 7, None, 3, 0, None)
                    if handle == ctypes.c_void_p(-1).value:
                        result['inaccessible'] += 1
                    else:
                        try:
                            standard = FileStandardInfo()
                            if not kernel.GetFileInformationByHandleEx(handle, 1, ctypes.byref(standard), ctypes.sizeof(standard)):
                                result['inaccessible'] += 1
                            else:
                                result['allocated_bytes'] += standard.AllocationSize
                        finally:
                            kernel.CloseHandle(handle)
                else:
                    result['allocated_bytes'] += info.st_blocks * 512
        except OSError:
            result['inaccessible'] += 1
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent-state', type=Path, required=True)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = {'read_only': True, 'existing_copies_deleted': 0,
              'application': usage(args.application), 'current_users': usage(args.application / 'Users')}
    for name in ('profile-backups', 'backups', 'downloads', 'transactions', 'profile-migration'):
        result[name] = usage(args.agent_state / name)
    result['all_agent_state'] = usage(args.agent_state)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
