#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find and start Open Office builds without hunting through build/.

    --list     every runnable build, newest first, with the default entry and the newest marked
    --newest   start the most recently built OpenOffice.exe (may be an unverified candidate)
    --default  start the verified default entry (the folder scripts/build_installer.py packages)

Nothing is moved, renamed or deleted; the answer is worked out afresh each time, so it stays
right as new candidates are staged or the default entry changes.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def runtimes():
    found = []
    for folder in BUILD.iterdir():
        if not folder.is_dir() or folder.name.startswith("."):
            continue
        if not (folder.name.startswith("full-source-run") or folder.name == "native-engine-run"):
            continue
        exe = folder / "OpenOffice.exe"
        if not exe.is_file():
            continue
        label = ""
        marker = folder / "openoffice-host-candidate.json"
        if marker.is_file():
            try:
                data = json.loads(marker.read_text(encoding="utf-8"))
                label = data.get("build_label") or data.get("host", {}).get("build_label") or ""
            except (OSError, ValueError):
                pass
        found.append({"folder": folder, "exe": exe, "built": datetime.fromtimestamp(exe.stat().st_mtime), "label": label})
    return sorted(found, key=lambda item: item["built"], reverse=True)


def default_entry():
    # The installer script names the default entry; README and the handoff document follow it.
    text = (ROOT / "scripts" / "build_installer.py").read_text(encoding="utf-8")
    match = re.search(r'^SOURCE = ROOT / "build" / "([^"]+)"', text, flags=re.M)
    return BUILD / match.group(1) if match else None


# Models downloaded during development. Installed copies keep models in their own folder;
# the build folders here share this one instead of a copy each (OPEN_OFFICE_MODEL_DIR).
MODELS = ROOT / "dependencies" / "local-models" / "download"


def start(item, title):
    print(f"正在启动{title}：{item['folder'].relative_to(ROOT)}（{item['built']:%Y-%m-%d %H:%M} 构建）")
    env = dict(os.environ)
    if MODELS.is_dir() and "OPEN_OFFICE_MODEL_DIR" not in env:
        env["OPEN_OFFICE_MODEL_DIR"] = str(MODELS)
        print(f"本地 AI 模型用 {MODELS.relative_to(ROOT)}（开发版共用）")
    flags = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    subprocess.Popen([str(item["exe"])], cwd=item["folder"], creationflags=flags, close_fds=True, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--list", action="store_true")
    choice.add_argument("--newest", action="store_true")
    choice.add_argument("--default", action="store_true")
    args = parser.parse_args()
    items = runtimes()
    if not items:
        print("build 文件夹里没有找到可以运行的 OpenOffice.exe。")
        return 1
    default = default_entry()
    if args.list:
        print("build 里可以运行的版本（新的在上）：\n")
        for index, item in enumerate(items):
            marks = []
            if index == 0:
                marks.append("最新")
            if default and item["folder"] == default:
                marks.append("稳定版·默认入口")
            mark = "  ← " + "、".join(marks) if marks else ""
            print(f"  {item['built']:%m-%d %H:%M}  {item['folder'].name:<42}{item['label']:<18}{mark}")
        print("\n“最新”可能还在验证中；“稳定版”是已经验证过、安装包也用它的版本。")
        return 0
    if args.newest:
        start(items[0], "最新版")
        return 0
    match = next((item for item in items if default and item["folder"] == default), None)
    if not match:
        print(f"没有找到默认入口 {default}。")
        return 1
    start(match, "稳定版")
    return 0


if __name__ == "__main__":
    sys.exit(main())
