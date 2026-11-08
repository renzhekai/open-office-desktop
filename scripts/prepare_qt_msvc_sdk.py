"""Prepare the two approved Qt components from verified official archives.

Creates a new SDK directory only. This is a manual SDK provision, not a
MaintenanceTool transaction; its registry and existing MinGW kit are untouched.
No system PATH changes or installer/license acceptance scripts are executed.
"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "evidence/qt-official-metadata/payload-manifest.json"
CACHE = ROOT / "dependencies/qt-6.11.1-msvc2022-archives"
STAGING = ROOT / "build/qt-sdk-staging/msvc2022_64"
TARGET = Path("D:/Qt/6.11.1/msvc2022_64")
TAR = "C:/Windows/System32/tar.exe"


def verify_archive(item):
    name = item["filename"]
    if Path(name).name != name or "/" in name or "\\" in name:
        raise ValueError("Invalid archive name")
    archive = CACHE / name
    with archive.open("rb") as stream:
        actual_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    if archive.stat().st_size != item["size"] or actual_hash != item["sha256"]:
        raise ValueError("Archive checksum/size mismatch: " + name)
    names = subprocess.run([TAR, "-tf", str(archive)], check=True,
                           capture_output=True, text=True, encoding="utf-8",
                           errors="surrogateescape").stdout.splitlines()
    listing = subprocess.run([TAR, "-tvf", str(archive)], check=True,
                             capture_output=True, text=True, encoding="utf-8",
                             errors="surrogateescape").stdout.splitlines()
    if not names or len(names) != len(listing):
        raise ValueError("Unexpected archive inventory: " + name)
    for member, detail in zip(names, listing):
        path = PurePosixPath(member.replace("\\", "/"))
        if (path.is_absolute() or ".." in path.parts or ":" in member
                or not detail or detail[0] not in "-d"):
            raise ValueError("Unsafe or linked archive member: " + member)
    if item["extractRelativeToSdk"] not in {".", "bin"}:
        raise ValueError("Unexpected extraction destination")
    return archive


def main():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8-sig"))
    if (manifest["qtVersion"] != "6.11.1" or len(manifest["archives"]) != 9
            or Path(manifest["sdkTarget"]) != TARGET):
        raise ValueError("Unexpected approved SDK target")
    if TARGET.exists() or STAGING.exists():
        raise FileExistsError("SDK or staging directory exists; inspect before recovery")
    if TARGET.parent.resolve() != Path("D:/Qt/6.11.1"):
        raise ValueError("Unexpected SDK parent or reparse target")
    archives = [(item, verify_archive(item)) for item in manifest["archives"]]
    STAGING.mkdir(parents=True)
    for item, archive in archives:
        destination = STAGING / item["extractRelativeToSdk"]
        destination.mkdir(exist_ok=True)
        subprocess.run([TAR, "-xf", str(archive), "-C", str(destination)], check=True)
        print("Extracted " + item["module"], flush=True)
    if (STAGING / "bin/qt6.conf").exists():
        raise ValueError("Archive supplies qt6.conf; inspect its precedence before configuring SDK")
    if (STAGING / "bin/qt.conf").exists():
        print("Original archive qt.conf: " + (STAGING / "bin/qt.conf").read_text(encoding="utf-8"))
    (STAGING / "bin/qt.conf").write_text(
        "[Paths]\nDocumentation=../../Docs/Qt-6.11.1\nExamples=../../Examples/Qt-6.11.1\nPrefix=..\n",
        encoding="utf-8")
    (STAGING / "bin/qtenv2.bat").write_text(
        '@echo off\nset "PATH=%~dp0;%PATH%"\ncd /D "%~dp0.."\n', encoding="ascii")
    # Same-volume rename, with an absent destination; no merge or replacement.
    if TARGET.exists():
        raise FileExistsError("SDK target appeared during extraction")
    os.rename(STAGING, TARGET)
    evidence = {
        "qt_version": manifest["qtVersion"], "path": str(TARGET),
        "provision_method": "verified_official_archives_manual_sdk",
        "maintenance_tool_registered": False, "license_prompt_accepted": False,
        "existing_mingw_modified": False, "runtime_and_compile_validation": "pending",
        "archives": [{"module": item["module"], "sha256": item["sha256"],
                      "source": item["source"]} for item, _ in archives],
    }
    with (ROOT / "evidence/qt-msvc-manual-sdk.json").open("x", encoding="utf-8") as stream:
        json.dump(evidence, stream, indent=2)
    print("Prepared manual SDK: " + str(TARGET), flush=True)


if __name__ == "__main__":
    main()
