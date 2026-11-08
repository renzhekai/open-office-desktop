"""Create a local development run directory from the pinned official runtime.

The entry point and Qt6 wrapper DLLs come from this project's builds. The native
engine, helper, converter and editor resources are the verified upstream release.
This script never launches DesktopEditors.exe or installs system components.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "dependencies/onlyoffice-v10.0.0/DesktopEditors_x64.zip"
TARGET = ROOT / "build/native-engine-run"
WRAPPERS = ROOT / "build/native-onlyoffice-qt6/bin"
QT_BIN = Path("D:/Qt/6.11.1/msvc2022_64/bin")
MARKER = TARGET / "openoffice-development-stage.json"
OWN_FILES = {
    "OpenOffice.exe": ROOT / "build/native-engine/bin/OpenOffice.exe",
    "openoffice_qt6_documentscore.dll": WRAPPERS / "openoffice_qt6_documentscore.dll",
    "openoffice_qt6_videoplayer.dll": WRAPPERS / "openoffice_qt6_videoplayer.dll",
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-own-binaries", action="store_true")
    args = parser.parse_args()
    expected = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8"))[
        "desktop_editors"]["windows_x64_baseline"]["sha256"]
    for path in OWN_FILES.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    if TARGET.resolve().parent != (ROOT / "build").resolve():
        raise ValueError("Unexpected stage target")
    if args.update_own_binaries:
        previous = json.loads(MARKER.read_text(encoding="utf-8"))
        if previous["official_archive_sha256"] != expected:
            raise ValueError("Stage uses a different official runtime")
        for name in OWN_FILES:
            if digest(TARGET / name) != previous["own_binary_sha256"][name]:
                raise ValueError("Staged binary changed outside this build: " + name)
        copied = previous["converter_dlls_copied_to_root"]
    else:
        if TARGET.exists():
            raise FileExistsError("Stage exists; inspect it or update only owned binaries")
        if digest(ARCHIVE) != expected:
            raise ValueError("Official runtime archive hash mismatch")
        with zipfile.ZipFile(ARCHIVE) as archive:
            for info in archive.infolist():
                name = info.filename.replace("\\", "/")
                parts = PurePosixPath(name)
                mode = (info.external_attr >> 16) & 0xffff
                if (parts.is_absolute() or ".." in parts.parts or ":" in name
                        or stat.S_ISLNK(mode)):
                    raise ValueError("Unsafe runtime archive path: " + name)
            TARGET.mkdir(parents=True)
            archive.extractall(TARGET)
        # The directly linked host needs these imports before main(). Keep the
        # converter's original layout too, for its own executable and resources.
        copied = []
        for source in sorted((TARGET / "converter").glob("*.dll")):
            destination = TARGET / source.name
            if destination.exists():
                if digest(destination) != digest(source):
                    raise ValueError("Different same-name runtime DLLs: " + source.name)
                continue
            shutil.copy2(source, destination)
            copied.append(source.name)
    for name, source in OWN_FILES.items():
        shutil.copy2(source, TARGET / name)
    deploy = [str(QT_BIN / "windeployqt.exe"), "--release", "--no-compiler-runtime",
              "--no-translations", "--dir", str(TARGET),
              "--plugindir", str(TARGET / "qt6-plugins")]
    deploy.extend(str(TARGET / name) for name in OWN_FILES)
    env = os.environ.copy()
    env["PATH"] = str(QT_BIN) + os.pathsep + env.get("PATH", "")
    subprocess.run(deploy, check=True, env=env)
    # Qt6-specific paths preserve the upstream Qt5 applications' plugin layout.
    (TARGET / "qt6.conf").write_text("[Paths]\nPrefix=.\nPlugins=qt6-plugins\nTranslations=translations\n", encoding="utf-8")
    shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", TARGET / "OPEN_OFFICE_THIRD_PARTY_NOTICES.md")
    notices = TARGET / "OPEN_OFFICE_SOURCE_LICENSES"
    notices.mkdir(exist_ok=True)
    for label, relative in {
        "ONLYOFFICE-DesktopEditors-LICENSE": "upstream/DesktopEditors/LICENSE",
        "ONLYOFFICE-desktop-sdk-LICENSE": "upstream/DesktopEditors/desktop-sdk/LICENSE",
        "ONLYOFFICE-core-LICENSE": "upstream/DesktopEditors/core/LICENSE",
    }.items():
        shutil.copy2(ROOT / relative, notices / label)
    report = {
        "purpose": "Local development build; native editor round trip requires separate validation",
        "entry_point": str(TARGET / "OpenOffice.exe"),
        "official_archive_sha256": expected,
        "engine_origin": "official_prebuilt_v10.0.0",
        "qt_wrapper_origin": "locally_compiled_pinned_source_with_qt6",
        "compiler_runtime": "uses the existing installed Microsoft VC runtime; no installer run",
        "converter_dlls_copied_to_root": copied,
        "own_binary_sha256": {name: digest(TARGET / name) for name in OWN_FILES},
    }
    MARKER.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
