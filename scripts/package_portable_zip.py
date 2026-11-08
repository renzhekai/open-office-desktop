#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Pack the default entry into a portable zip, verify it, and optionally test an extracted copy.

The zip holds the runtime folder exactly as verified (without its openoffice-profile user state)
under a top-level OpenOffice/ folder, plus a generated 使用说明.txt. It is written to a temporary
name, re-read entry by entry (CRC and size against the source files), then renamed; an existing
zip is never overwritten and the source runtime is never modified. --check-launch extracts the zip
into a new folder under build/portable-check and starts the extracted OpenOffice.exe the way a
double-click would (no command-line runtime/profile options, minimal environment, a different
working directory), with a timer that closes it; that copy then writes its own openoffice-profile.

Usage:
    python -I scripts/package_portable_zip.py --check-launch
"""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build" / "full-source-run-host-saveas-r2"
DIST = ROOT / "dist"
EVIDENCE = ROOT / "evidence" / "portable"
CHECK = ROOT / "build" / "portable-check"
SKIP_TOP_LEVEL = {"openoffice-profile"}
TOP = "OpenOffice"
GUIDE = "使用说明.txt"
GUIDE_TEXT = """Open Office 便携版（开发版，{date} 打包）

使用方法
1. 先把整个压缩包解压到一个文件夹，不要在压缩包里直接双击运行。
2. 打开解压出的 OpenOffice 文件夹，双击 OpenOffice.exe。
3. 不要只拷走 OpenOffice.exe，它需要同一文件夹里的其它文件。
   想放到桌面，可右键 OpenOffice.exe，选“发送到 → 桌面快捷方式”。

运行前提
- 64 位 Windows 10 或 11。
- 需要已安装 Microsoft Visual C++ 2015-2022 Redistributable (x64)。本包不含这个运行库；没有它时程序无法启动，请从微软官网下载安装。
- 请解压到有写入权限的位置（例如“文档”或 D 盘），不要放在 C:\\Program Files。
  第一次启动会在 OpenOffice 文件夹里新建 openoffice-profile，保存配置和缓存。

当前功能
- 打开、编辑、保存现有 DOCX。
- 新建文字文档、另存为、在同一窗口换文档。
- 暂未开放：新建或打开表格/演示、导出为 PDF 等其他格式、多文档标签页。

这是开发版，只在打包这台电脑上验证过，还没有在其他电脑上测试。
基于 ONLYOFFICE（Ascensio System SIA）开源组件。第三方许可见 OPEN_OFFICE_THIRD_PARTY_NOTICES.md 和 OPEN_OFFICE_SOURCE_LICENSES 文件夹。
本项目不是 ONLYOFFICE 官方发行版。
"""


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def source_files():
    for directory, dirnames, filenames in os.walk(SOURCE):
        rel = Path(directory).relative_to(SOURCE)
        if rel == Path("."):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_TOP_LEVEL)
        else:
            dirnames.sort()
        for name in sorted(filenames):
            yield (rel / name).as_posix(), Path(directory) / name


def check_launch(zip_path, report):
    target = CHECK / zip_path.stem
    if target.exists():
        sys.exit(f"{target} exists; refusing to reuse an extraction folder")
    target.mkdir(parents=True)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(target)
    app = target / TOP
    executable = app / "OpenOffice.exe"
    smoke = target / "launch-report.json"
    env = {key: os.environ[key] for key in ("SystemRoot", "SystemDrive", "WINDIR", "USERPROFILE", "USERNAME",
                                            "TEMP", "TMP", "APPDATA", "LOCALAPPDATA", "PROGRAMDATA",
                                            "COMPUTERNAME", "HOMEDRIVE", "HOMEPATH")
           if key in os.environ}
    env["PATH"] = os.pathsep.join([str(Path(os.environ["SystemRoot"]) / "System32"), os.environ["SystemRoot"]])
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    # No --engine-profile/--onlyoffice-runtime: the defaults a double-click uses. The working
    # directory is the extraction parent, not the app folder.
    result = subprocess.run([str(executable), "--smoke-report", str(smoke), "--exit-after-ms", "9000"],
                            cwd=target, env=env, capture_output=True, timeout=180)
    launch = json.loads(smoke.read_text(encoding="utf-8")) if smoke.is_file() else None
    profile = app / "openoffice-profile"
    report["launch_check"] = {
        "extracted_to": str(target), "started_utc": started, "exit_code": result.returncode,
        "stderr_tail": result.stderr.decode("utf-8", "replace")[-2000:],
        "environment_path": env["PATH"], "working_directory": str(target),
        "launch_report": launch,
        "default_profile_created": profile.is_dir(),
        "ok": result.returncode == 0 and bool(launch) and launch.get("engine_connected") is True
              and launch.get("window_rendered") is True and profile.is_dir(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check-launch", action="store_true")
    args = parser.parse_args()
    marker = json.loads((SOURCE / "openoffice-host-candidate.json").read_text(encoding="utf-8"))
    if sha256_file(SOURCE / "OpenOffice.exe") != marker["host"]["sha256"]:
        sys.exit("the source runtime host is not the recorded one")
    date = datetime.date.today().strftime("%Y%m%d")
    zip_path = DIST / f"OpenOffice-portable-win64-{date}.zip"
    report_path = EVIDENCE / f"{zip_path.stem}.json"
    for path in (zip_path, report_path):
        if path.exists():
            sys.exit(f"{path} exists; refusing to overwrite")
    DIST.mkdir(exist_ok=True)
    partial = zip_path.with_name(zip_path.name + ".partial")
    if partial.exists():
        sys.exit(f"{partial} exists; inspect it before packing again")

    files = list(source_files())
    guide = GUIDE_TEXT.format(date=datetime.date.today().isoformat()).replace("\n", "\r\n")
    with zipfile.ZipFile(partial, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6,
                         allowZip64=True) as archive:
        archive.writestr(f"{TOP}/{GUIDE}", b"\xef\xbb\xbf" + guide.encode("utf-8"))
        for rel, path in files:
            archive.write(path, f"{TOP}/{rel}")

    # Re-read: every entry's CRC (testzip) and size must match its source file.
    mismatches = []
    with zipfile.ZipFile(partial) as archive:
        bad = archive.testzip()
        infos = {info.filename: info for info in archive.infolist()}
        for rel, path in files:
            info = infos.get(f"{TOP}/{rel}")
            if info is None or info.file_size != path.stat().st_size:
                mismatches.append(rel)
        exe_in_zip = hashlib.sha256(archive.read(f"{TOP}/OpenOffice.exe")).hexdigest()
    if bad or mismatches or len(infos) != len(files) + 1 or exe_in_zip != marker["host"]["sha256"]:
        sys.exit(f"verification failed (crc: {bad}, mismatches: {mismatches[:5]}, entries: {len(infos)}); "
                 f"partial kept at {partial}")
    partial.rename(zip_path)
    zip_sha = sha256_file(zip_path)
    (zip_path.with_name(zip_path.name + ".sha256.txt")).write_text(f"{zip_sha}  {zip_path.name}\n",
                                                                   encoding="ascii")

    report = {
        "kind": "open-office-portable-zip",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "zip": str(zip_path), "zip_bytes": zip_path.stat().st_size, "zip_sha256": zip_sha,
        "source_runtime": str(SOURCE),
        "source_host_candidate_marker_sha256": sha256_file(SOURCE / "openoffice-host-candidate.json"),
        "host_sha256": marker["host"]["sha256"],
        "files_from_runtime": len(files),
        "source_bytes": sum(path.stat().st_size for _, path in files),
        "skipped_top_level": sorted(SKIP_TOP_LEVEL),
        "added": [f"{TOP}/{GUIDE}"],
        "crc_and_size_verified": True,
        "external_requirements": {
            "vc_runtime": "Microsoft Visual C++ 2015-2022 Redistributable (x64): msvcp140*.dll and "
                          "vcruntime140*.dll are imported by 69 bundled PE files and are not in the package",
            "windows": "64-bit Windows 10/11 system DLLs (cryptui, d3d12, dxva2, evr, gdiplus, icuuc, "
                       "imagehlp, mswsock, winspool.drv and API sets)",
        },
        "tested_on_other_machine": False,
    }
    if args.check_launch:
        check_launch(zip_path, report)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with open(report_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"zip": str(zip_path), "zip_bytes": report["zip_bytes"], "zip_sha256": zip_sha,
                      "files": len(files) + 1,
                      "launch_check_ok": report.get("launch_check", {}).get("ok")}, ensure_ascii=False))
    return 0 if report.get("launch_check", {}).get("ok", True) else 1


if __name__ == "__main__":
    sys.exit(main())
