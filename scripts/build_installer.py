#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build the Open Office installer with the project-local Inno Setup 6.7.4 and optionally test it.

Inputs are the verified default-entry runtime, the Visual C++ runtime DLLs from the local Visual
Studio VC\\Redist folder and installer/OpenOffice.iss (with its Chinese messages and notes). The
compiler's signature, the runtime host and the CRT files are checked first; an existing setup or
evidence file is never overwritten. --test installs the result silently into a new folder under
build/installer-check (no shortcuts), compares every installed file with its source, starts the
installed OpenOffice.exe the way a double-click would and records which msvcp140.dll it loaded,
then uninstalls silently and checks that the uninstall entry and installed files are gone.

Usage:
    python -I scripts/build_installer.py --test
    python -I scripts/build_installer.py --test --same-day 2   # a second build on the same day: ...-yyyymmdd-2-win64
"""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build" / "full-source-run-builtin-ai-r9"  # the default entry; --source overrides
CRT = Path(r"D:\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT")
CRT_FILES = ["msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll"]
CRT_DESTINATIONS = ["", "converter"]
ISCC = ROOT / "dependencies" / "innosetup-6.7.4" / "install" / "ISCC.exe"
SCRIPT = ROOT / "installer" / "OpenOffice.iss"
TEXT_FILES = [SCRIPT, ROOT / "installer" / "ChineseSimplified.isl", ROOT / "installer" / "安装前说明.txt"]
ENGINE_LICENSES = ROOT / "installer" / "third-party" / "llama.cpp-LICENSES.txt"  # installed into ai-engine
DIST = ROOT / "dist"
EVIDENCE = ROOT / "evidence" / "installer"
CHECK = ROOT / "build" / "installer-check"
APP_ID = "{ABE6780B-54BF-4D7B-B666-31FA29478AAA}"
CHECK_NAME = None  # folder name under build/installer-check; the setup name unless retesting
UNINSTALL_KEY = rf"HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{APP_ID}_is1"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def powershell(command):
    return subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True,
                          text=True, encoding="utf-8", errors="replace").stdout.strip()


def signature(path):
    out = powershell(f"$s = Get-AuthenticodeSignature -LiteralPath '{path}'; "
                     "$s.Status.ToString() + '|' + $s.SignerCertificate.Subject")
    status, _, subject = out.partition("|")
    return {"status": status, "subject": subject}


def file_version(path):
    return powershell(f"(Get-Item -LiteralPath '{path}').VersionInfo.FileVersion")


def registry(key):
    result = subprocess.run(["reg", "query", key], capture_output=True, text=True, encoding="mbcs",
                            errors="replace")
    if result.returncode != 0:
        return None
    values = {}
    for line in result.stdout.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and parts[1].startswith("REG_"):
            values[parts[0]] = parts[2]
    return values


def runtime_files():
    for directory, dirnames, filenames in os.walk(SOURCE):
        rel = Path(directory).relative_to(SOURCE)
        if rel == Path("."):
            dirnames[:] = [d for d in dirnames if d != "openoffice-profile"]
        for name in filenames:
            yield (rel / name).as_posix(), Path(directory) / name


def ensure_bom(path):
    data = path.read_bytes()
    if not data.startswith(b"\xef\xbb\xbf"):
        data.decode("utf-8")  # must already be UTF-8
        path.write_bytes(b"\xef\xbb\xbf" + data)  # Inno Setup reads BOM-less files as ANSI


def test_installer(setup, report):
    target = CHECK / (CHECK_NAME or setup.stem)
    if target.exists():
        sys.exit(f"{target} exists; refusing to reuse a test folder")
    if registry(UNINSTALL_KEY) is not None:
        sys.exit("Open Office is already registered as installed for this user; not testing over it")
    target.mkdir(parents=True)
    app = target / "app"
    log = target / "setup.log"
    started = time.time()
    install = subprocess.run([str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
                              "/TASKS=", f"/DIR={app}", f"/LOG={log}"], timeout=1800)
    installed_after = round(time.time() - started)
    entry = None
    deadline = time.time() + 60  # do not depend on exactly when the loader process returns
    while entry is None and time.time() < deadline:
        entry = registry(UNINSTALL_KEY)
        if entry is None:
            time.sleep(2)
    test = {"folder": str(target), "install_exit_code": install.returncode,
            "install_seconds": installed_after,
            "uninstall_entry_seen_after_seconds": round(time.time() - started) if entry else None,
            "uninstall_entry_after_install": entry}

    mismatches = []
    expected = set()
    for rel, path in runtime_files():
        expected.add(rel)
        installed = app / rel
        if not installed.is_file() or installed.stat().st_size != path.stat().st_size or sha256(installed) != sha256(path):
            mismatches.append(rel)
    for destination in CRT_DESTINATIONS:
        for name in CRT_FILES:
            rel = f"{destination}/{name}" if destination else name
            expected.add(rel)
            if not (app / rel).is_file() or sha256(app / rel) != sha256(CRT / name):
                mismatches.append(rel)
    installed_files = {p.relative_to(app).as_posix() for p in app.rglob("*") if p.is_file()}
    test["installed_files"] = len(installed_files)
    test["file_mismatches"] = mismatches[:20]
    test["extra_files"] = sorted(installed_files - expected)
    expected_extra = ["unins000.dat", "unins000.exe"]
    if (SOURCE / "ai-engine").is_dir():
        expected_extra.append("ai-engine/llama.cpp-LICENSES.txt")
        licence = app / "ai-engine" / "llama.cpp-LICENSES.txt"
        test["engine_licenses_installed"] = licence.is_file() and sha256(licence) == sha256(ENGINE_LICENSES)
    test["expected_extra_files"] = sorted(expected_extra)

    smoke = target / "launch-report.json"
    env = {key: os.environ[key] for key in ("SystemRoot", "SystemDrive", "WINDIR", "USERPROFILE", "USERNAME",
                                            "TEMP", "TMP", "APPDATA", "LOCALAPPDATA", "PROGRAMDATA",
                                            "COMPUTERNAME", "HOMEDRIVE", "HOMEPATH") if key in os.environ}
    env["PATH"] = os.pathsep.join([str(Path(os.environ["SystemRoot"]) / "System32"), os.environ["SystemRoot"]])
    process = subprocess.Popen([str(app / "OpenOffice.exe"), "--smoke-report", str(smoke), "--exit-after-ms", "12000"],
                               cwd=target, env=env)
    time.sleep(6)
    loaded = powershell("(Get-Process -Id %d).Modules | Where-Object { $_.ModuleName -like 'msvcp140*' -or "
                        "$_.ModuleName -like 'vcruntime140*' } | ForEach-Object { $_.FileName }" % process.pid)
    exit_code = process.wait(timeout=180)
    launch = json.loads(smoke.read_text(encoding="utf-8")) if smoke.is_file() else None
    loaded_paths = [line for line in loaded.splitlines() if line]
    test["launch"] = {"exit_code": exit_code, "report": launch,
                      "crt_modules_loaded": loaded_paths,
                      "crt_loaded_from_app": bool(loaded_paths) and all(
                          Path(p).resolve().parent == app.resolve() for p in loaded_paths),
                      "default_profile_created": (app / "openoffice-profile").is_dir()}

    uninstaller = app / "unins000.exe"
    subprocess.run([str(uninstaller), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=600)
    deadline = time.time() + 300  # the uninstaller relaunches itself from a temporary copy
    while time.time() < deadline and (registry(UNINSTALL_KEY) is not None or uninstaller.exists()):
        time.sleep(2)
    remaining = sorted(p.relative_to(app).as_posix() for p in app.rglob("*") if p.is_file()) if app.exists() else []
    test["uninstall"] = {"entry_removed": registry(UNINSTALL_KEY) is None,
                         "remaining_files": len(remaining),
                         "remaining_outside_profile": [p for p in remaining if not p.startswith("openoffice-profile/")][:20]}
    test["ok"] = (install.returncode == 0 and bool(test["uninstall_entry_after_install"]) and not mismatches
                  and test["extra_files"] == test["expected_extra_files"]
                  and test.get("engine_licenses_installed", True)
                  and exit_code == 0 and bool(launch) and launch.get("engine_connected") is True
                  and test["launch"]["crt_loaded_from_app"] and test["launch"]["default_profile_created"]
                  and test["uninstall"]["entry_removed"] and not test["uninstall"]["remaining_outside_profile"])
    report["install_test"] = test


def retest(setup):
    build_report = EVIDENCE / f"{setup.stem}.json"
    built = json.loads(build_report.read_text(encoding="utf-8"))
    if sha256(setup) != built["setup_sha256"]:
        sys.exit("the setup does not match its build record")
    number = 1
    while (EVIDENCE / f"{setup.stem}-retest-{number}.json").exists() or (CHECK / f"{setup.stem}-retest-{number}").exists():
        number += 1
    name = f"{setup.stem}-retest-{number}"
    report = {"kind": "open-office-installer-retest", "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "setup": str(setup), "setup_sha256": built["setup_sha256"], "build_record": str(build_report)}
    global CHECK_NAME
    CHECK_NAME = name
    test_installer(setup, report)
    with open(EVIDENCE / f"{name}.json", "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"retest": name, "test_ok": report["install_test"]["ok"]}, ensure_ascii=False))
    return 0 if report["install_test"]["ok"] else 1


def main():
    global SOURCE
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--source", type=Path, help="runtime folder to package (default: %s)" % SOURCE)
    parser.add_argument("--same-day", type=int, metavar="N",
                        help="Nth build of the day (N >= 2): adds -N to the date so the day's earlier setup is kept")
    parser.add_argument("--retest", type=Path, metavar="SETUP_EXE",
                        help="test an already built setup again (checked against its build record) without compiling")
    args = parser.parse_args()
    if args.source:
        SOURCE = (args.source if args.source.is_absolute() else ROOT / args.source).resolve()
    if args.retest:
        return retest(args.retest.resolve())
    marker = json.loads((SOURCE / "openoffice-host-candidate.json").read_text(encoding="utf-8"))
    if sha256(SOURCE / "OpenOffice.exe") != marker["host"]["sha256"]:
        sys.exit("the source runtime host is not the recorded one")
    compiler_signature = signature(ISCC)
    if compiler_signature["status"] != "Valid" or "Pyrsys B.V." not in compiler_signature["subject"]:
        sys.exit(f"ISCC.exe signature not valid: {compiler_signature}")
    crt = {}
    for name in CRT_FILES:
        path = CRT / name
        crt[name] = {"sha256": sha256(path), "version": file_version(path), "signature": signature(path)}
        if crt[name]["signature"]["status"] != "Valid":
            sys.exit(f"{path} signature not valid")

    date = datetime.date.today().strftime("%Y%m%d")
    if args.same_day is not None:
        if args.same_day < 2:
            sys.exit("--same-day starts at 2; the first build of a day has no number")
        date += f"-{args.same_day}"
    stem = f"OpenOffice-Setup-0.1.0-dev-{date}-win64"
    setup = DIST / f"{stem}.exe"
    report_path = EVIDENCE / f"{stem}.json"
    log_path = EVIDENCE / f"{stem}-iscc.log"
    for path in (setup, report_path, log_path):
        if path.exists():
            sys.exit(f"{path} exists; refusing to overwrite")
    for path in TEXT_FILES:
        ensure_bom(path)
    EVIDENCE.mkdir(parents=True, exist_ok=True)

    started = time.time()
    compile_result = subprocess.run([str(ISCC), f"/DSourceDir={SOURCE}", f"/DCrtDir={CRT}", f"/DOutputDir={DIST}",
                                     f"/DBuildDate={date}", str(SCRIPT)], capture_output=True)
    log_path.write_bytes(compile_result.stdout + b"\n--- stderr ---\n" + compile_result.stderr)
    if compile_result.returncode != 0 or not setup.is_file():
        sys.exit(f"ISCC failed with {compile_result.returncode}; see {log_path}")

    report = {
        "kind": "open-office-installer",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "setup": str(setup), "setup_bytes": setup.stat().st_size, "setup_sha256": sha256(setup),
        "setup_signed": False,
        "compile_seconds": round(time.time() - started),
        "compiler": {"path": str(ISCC), "version": file_version(ISCC), "signature": compiler_signature},
        "source_runtime": str(SOURCE),
        "source_host_candidate_marker_sha256": sha256(SOURCE / "openoffice-host-candidate.json"),
        "host_sha256": marker["host"]["sha256"],
        "runtime_files": sum(1 for _ in runtime_files()),
        "crt_dir": str(CRT), "crt_files": crt, "crt_destinations": ["{app}", "{app}\\converter"],
        "crt_license": "Visual Studio Distributable Code: files in VC\\Redist may be distributed unmodified with "
                       "the program (https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)",
        "script_sha256": {p.name: sha256(p) for p in TEXT_FILES},
        "engine_licenses_sha256": sha256(ENGINE_LICENSES) if (SOURCE / "ai-engine").is_dir() else None,
        "iscc_log": str(log_path),
        "tested_on_other_machine": False,
    }
    if args.test:
        test_installer(setup, report)
    with open(report_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    (setup.with_name(setup.name + ".sha256.txt")).write_text(f"{report['setup_sha256']}  {setup.name}\n",
                                                             encoding="ascii")
    summary = {"setup": str(setup), "bytes": report["setup_bytes"], "sha256": report["setup_sha256"],
               "compile_seconds": report["compile_seconds"], "test_ok": report.get("install_test", {}).get("ok")}
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if report.get("install_test", {}).get("ok", True) else 1


if __name__ == "__main__":
    sys.exit(main())
