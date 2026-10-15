"""Compare inspection of the active Python DLL with Windows version APIs."""

from __future__ import annotations

import ctypes
import json
import sys
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))


def main() -> None:
    if sys.platform != "win32":
        raise SystemExit("This check requires Windows.")
    from harrancraft_tools.pe import inspect_file

    image = Path(sys.base_prefix) / f"python{sys.version_info.major}{sys.version_info.minor}.dll"
    report = inspect_file(image, include_exports=True)
    version = ctypes.WinDLL("version", winmode=0x800)
    size_fn = version.GetFileVersionInfoSizeW
    size_fn.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    size_fn.restype = wintypes.DWORD
    get_fn = version.GetFileVersionInfoW
    get_fn.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    get_fn.restype = wintypes.BOOL
    query_fn = version.VerQueryValueW
    query_fn.argtypes = [
        ctypes.c_void_p,
        wintypes.LPCWSTR,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(wintypes.UINT),
    ]
    query_fn.restype = wintypes.BOOL
    ignored = wintypes.DWORD()
    size = size_fn(str(image), ctypes.byref(ignored))
    if not size:
        raise RuntimeError("Windows version size query failed")
    buffer = ctypes.create_string_buffer(size)
    if not get_fn(str(image), 0, size, buffer):
        raise RuntimeError("Windows version read failed")
    pointer = ctypes.c_void_p()
    length = wintypes.UINT()
    if not query_fn(buffer, "\\", ctypes.byref(pointer), ctypes.byref(length)) or length.value < 52:
        raise RuntimeError("Windows fixed version query failed")
    fixed = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD))
    ms, ls = fixed[2], fixed[3]
    expected = f"{ms >> 16}.{ms & 65535}.{ls >> 16}.{ls & 65535}"
    if expected not in report["file_versions"]:
        raise RuntimeError("PE parser and Windows version API disagree")
    names = {name for item in report["exports"] for name in item["names"]}
    if "Py_Initialize" not in names or report["kind"] != "dll":
        raise RuntimeError("Python DLL export inspection failed")
    print(
        json.dumps(
            {
                "source": "active CPython DLL",
                "file_versions": report["file_versions"],
                "export_count": report["export_count"],
                "native_version_comparison": "passed",
            }
        )
    )


if __name__ == "__main__":
    main()
