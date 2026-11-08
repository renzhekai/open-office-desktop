"""Read-only prerequisite inventory. Does not install, download, or edit system settings."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def command_version(name: str, args: list[str]) -> dict:
    path = shutil.which(name)
    result = {"path": path, "available_on_path": bool(path)}
    if not path:
        return result
    try:
        run = subprocess.run([path, *args], capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=15)
        result.update(exit_code=run.returncode, output=(run.stdout + run.stderr).strip())
    except (OSError, subprocess.TimeoutExpired) as exc:
        result["error"] = str(exc)
    return result


def memory() -> dict:
    if os.name != "nt":
        return {"status": "not_collected"}
    class MemoryStatus(ctypes.Structure):
        _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                    ("total_phys", ctypes.c_ulonglong), ("avail_phys", ctypes.c_ulonglong),
                    ("total_page", ctypes.c_ulonglong), ("avail_page", ctypes.c_ulonglong),
                    ("total_virtual", ctypes.c_ulonglong), ("avail_virtual", ctypes.c_ulonglong),
                    ("avail_extended", ctypes.c_ulonglong)]
    status = MemoryStatus()
    status.length = ctypes.sizeof(status)
    if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return {"status": "observed", "physical_bytes": status.total_phys,
                "available_bytes": status.avail_phys}
    return {"status": "unavailable"}


def directories(path: str) -> list[str]:
    root = Path(path)
    if not root.is_dir():
        return []
    return sorted(str(p) for p in root.iterdir() if p.is_dir())


def collect() -> dict:
    disk = shutil.disk_usage(ROOT)
    vs_root = Path(r"D:\Microsoft Visual Studio\18\Community")
    qt_kits = directories(r"D:\Qt\6.11.1")
    return {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "workspace": str(ROOT),
        "platform": {"system": platform.system(), "release": platform.release(),
                     "version": platform.version(), "machine": platform.machine()},
        "requirements_sha256": hashlib.sha256((ROOT / "requirements.md").read_bytes()).hexdigest(),
        "memory": memory(),
        "workspace_disk": {"total_bytes": disk.total, "free_bytes": disk.free},
        "tools": {name: command_version(name, args) for name, args in {
            "git": ["--version"], "python": ["--version"], "node": ["--version"],
            "cmake": ["--version"], "ninja": ["--version"], "qmake": ["-v"],
            "msbuild": ["-version"], "cl": []}.items()},
        "known_installations": {
            "visual_studio": str(vs_root) if vs_root.is_dir() else None,
            "msvc_toolsets": directories(str(vs_root / "VC/Tools/MSVC")),
            "qt_6_11_1_kits": qt_kits,
            "windows_sdk_include": directories(r"D:\Windows Kits\10\Include"),
            "onlyoffice_standard_paths": {path: Path(path).is_dir() for path in [
                r"C:\Program Files\ONLYOFFICE", r"C:\Program Files (x86)\ONLYOFFICE"]}},
        "limitations": [
            "Known installation locations and PATH are inventoried; absence here is not a full-disk search.",
            "Installed tool versions do not establish upstream compatibility or build success.",
            "No network requests, installation, system configuration, source builds, or desktop launches occur."
        ],
        "build_status": "not_run",
        "desktop_runtime_status": "not_run"
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="New JSON path inside this workspace")
    args = parser.parse_args()
    data = collect()
    text = json.dumps(data, ensure_ascii=False, indent=2)
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to(ROOT):
            parser.error("Output must stay inside the project workspace")
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as stream:
            stream.write(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
