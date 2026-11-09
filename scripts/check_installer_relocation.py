#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check 设置 → 迁移 against the real installer: install, move, uninstall from the new place.

Compiles installer/OpenOffice.iss (the real script and AppId) around a tiny stand-in program
folder, installs it for this user without shortcuts under build/installer-relocation-check/<label>,
adds what the program creates later (editor profile, a model), moves the folder with the helper
script from app/ui/relocation.cpp, checks the uninstall entry now names the new place, then
uninstalls from there and checks nothing is left at either place. A second install that is not
moved is uninstalled too. Refuses to run when Open Office is installed for this user.

Usage:
    python -I scripts/check_installer_relocation.py <label>
"""

import argparse
import base64
import datetime
import json
import re
import subprocess
import sys
import time
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISCC = ROOT / "dependencies" / "innosetup-6.7.4" / "install" / "ISCC.exe"
SCRIPT = ROOT / "installer" / "OpenOffice.iss"
HELPER_SOURCE = ROOT / "app" / "ui" / "relocation.cpp"
# Same folder as scripts/build_installer.py packs.
CRT = Path(r"D:\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT")
CHECK = ROOT / "build" / "installer-relocation-check"
EVIDENCE = ROOT / "evidence" / "installer"
KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{ABE6780B-54BF-4D7B-B666-31FA29478AAA}_is1"


def entry():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY) as key:
            values, index = {}, 0
            while True:
                try:
                    name, value, _ = winreg.EnumValue(key, index)
                except OSError:
                    return values
                values[name] = value
                index += 1
    except FileNotFoundError:
        return None


def wait(condition, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if condition():
            return True
        time.sleep(1)
    return condition()


def ps_quoted(text):
    return "'" + str(text).replace("'", "''") + "'"


def finished_pid():
    process = subprocess.Popen(["cmd.exe", "/c", "exit"])
    process.wait()
    return process.pid


def relocate(source, target, log):
    """Runs the helper exactly as the program builds it (relocationCommand), without relaunching."""
    text = HELPER_SOURCE.read_text(encoding="utf-8")
    script = re.search(r'R"PS\((.*?)\)PS"', text, re.S).group(1)
    prefix = (f"$ProcessId = {finished_pid()}\n$Source = {ps_quoted(source)}\n$Target = {ps_quoted(target)}\n"
              f"$Log = {ps_quoted(log)}\n$Launch = $false\n$ShortcutFolders = @()\n"
              f"$UninstallKey = 'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall'\n")
    encoded = base64.b64encode((prefix + script).encode("utf-16-le")).decode("ascii")
    powershell = Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
    result = subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                             "-EncodedCommand", encoded], timeout=300)
    lines = log.read_text(encoding="utf-8-sig").splitlines() if log.is_file() else []
    return result.returncode, lines


def install(setup, folder, log):
    result = subprocess.run([str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS", "/TASKS=",
                             f"/DIR={folder}", f"/LOG={log}"], timeout=600)
    seen = wait(lambda: entry() is not None, 60)
    # What the program writes later, which the uninstaller must also remove.
    (folder / "models").mkdir(exist_ok=True)
    (folder / "models" / "Qwen3-1.7B-Q8_0.gguf").write_bytes(b"m" * 4096)
    (folder / "openoffice-profile").mkdir(exist_ok=True)
    (folder / "openoffice-profile" / "native-events.log").write_text("x", encoding="utf-8")
    return {"exit_code": result.returncode, "entry_seen": seen, "entry": entry()}


def uninstall(folder):
    uninstaller = folder / "unins000.exe"
    result = subprocess.run([str(uninstaller), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=600)
    gone = wait(lambda: entry() is None and not folder.exists(), 120)
    left = sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*")) if folder.exists() else []
    return {"exit_code": result.returncode, "entry_removed": entry() is None, "folder_removed": not folder.exists(),
            "left": left[:20], "settled": gone}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("label")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9-]+", args.label):
        parser.error("label must be lowercase letters, digits and hyphens")
    work = CHECK / args.label
    report_path = EVIDENCE / f"relocation-check-{args.label}.json"
    if work.exists() or report_path.exists():
        sys.exit(f"{work} or {report_path} exists; refusing to reuse")
    if entry() is not None:
        sys.exit("Open Office is installed for this user; not testing over it")

    runtime = work / "runtime"
    (runtime / "converter").mkdir(parents=True)
    (runtime / "OpenOffice.exe").write_bytes(b"MZ stand-in, never started")
    (runtime / "converter" / "x2t.exe").write_bytes(b"MZ stand-in")
    compiled = subprocess.run([str(ISCC), f"/DSourceDir={runtime}", f"/DCrtDir={CRT}", f"/DOutputDir={work}",
                               "/DBuildDate=relocation-check", str(SCRIPT)], capture_output=True)
    (work / "iscc.log").write_bytes(compiled.stdout + compiled.stderr)
    if compiled.returncode != 0:
        sys.exit(f"ISCC failed with {compiled.returncode}; see {work / 'iscc.log'}")
    setup = next(work.glob("OpenOffice-Setup-*.exe"))

    report = {"checked_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "label": args.label,
              "script": str(SCRIPT), "helper_source": str(HELPER_SOURCE), "setup": str(setup)}

    # Moved: install on one place, move, uninstall from the new place.
    source = work / "first" / "OpenOffice"
    (work / "moved").mkdir()
    target = work / "moved" / "OpenOffice"
    moved = {"install": install(setup, source, work / "first-setup.log")}
    code, lines = relocate(source, target, work / "relocation.log")
    after = entry() or {}
    moved["relocation"] = {"exit_code": code, "log": lines,
                           "old_folder_gone": not source.exists(), "new_folder_has_program": (target / "OpenOffice.exe").is_file(),
                           "model_moved": (target / "models" / "Qwen3-1.7B-Q8_0.gguf").is_file(),
                           "entry_after_move": after}
    moved["relocation"]["entry_points_to_new_place"] = (
        after.get("Inno Setup: App Path", "").rstrip("\\").lower() == str(target).lower()
        and after.get("InstallLocation", "").rstrip("\\").lower() == str(target).lower()
        and str(target).lower() in after.get("UninstallString", "").lower())
    moved["uninstall"] = uninstall(target)
    moved["ok"] = (moved["install"]["exit_code"] == 0 and moved["install"]["entry_seen"]
                   and "RESULT moved" in lines and moved["relocation"]["old_folder_gone"]
                   and moved["relocation"]["model_moved"] and moved["relocation"]["entry_points_to_new_place"]
                   and moved["uninstall"]["entry_removed"] and moved["uninstall"]["folder_removed"]
                   and not source.exists())
    report["moved"] = moved

    # Not moved: the profile and models must go with the uninstall too.
    plain_folder = work / "plain" / "OpenOffice"
    plain = {"install": install(setup, plain_folder, work / "plain-setup.log")}
    plain["uninstall"] = uninstall(plain_folder)
    plain["ok"] = (plain["install"]["exit_code"] == 0 and plain["install"]["entry_seen"]
                   and plain["uninstall"]["entry_removed"] and plain["uninstall"]["folder_removed"])
    report["plain"] = plain
    report["ok"] = moved["ok"] and plain["ok"]

    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with open(report_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"ok": report["ok"], "moved_ok": moved["ok"], "plain_ok": plain["ok"],
                      "report": str(report_path)}, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
