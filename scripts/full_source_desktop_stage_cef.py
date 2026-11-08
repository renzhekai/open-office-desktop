"""Stage the previously verified fixed CEF archive into the isolated source tree."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "build/full-source"
ARCHIVE = ROOT / "dependencies/onlyoffice-cef-5414-win64/cef_binary.7z"
SHA256 = "0ef79ad22cc1b2e9364e6a165d1ee00f9348b46f6bb94615a48ed402145f741a"
EVIDENCE = AREA / "desktop-build-evidence"
EXTRACTED = EVIDENCE / "cef-5414-extracted"
DESTINATION = AREA / "DesktopEditors/core/Common/3dParty/cef/win_64/build"
TAR = r"C:\Windows\System32\tar.exe"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    if digest(ARCHIVE) != SHA256:
        raise ValueError("CEF archive differs from the previously verified official Git blob")
    listing = subprocess.run([TAR, "-tf", str(ARCHIVE)], capture_output=True, check=True).stdout.decode("utf-8").splitlines()
    verbose = subprocess.run([TAR, "-tvf", str(ARCHIVE)], capture_output=True, check=True).stdout.decode("utf-8", errors="replace")
    if any(line and line[0] not in "d-" for line in verbose.splitlines()):
        raise ValueError("CEF archive contains a nonregular member")
    names = []
    for name in listing:
        parsed = PurePosixPath(name)
        if parsed.is_absolute() or ".." in parsed.parts or ":" in name or "\\" in name:
            raise ValueError("Unsafe archive member")
        if not name.endswith("/"):
            if len(parsed.parts) < 3 or parsed.parts[:2] not in (
                    ("cef_binary", "Release"), ("cef_binary", "Resources")):
                raise ValueError("Unexpected CEF archive layout")
            names.append(name)
    if len(set(names)) != len(names):
        raise ValueError("Duplicate archive member")
    for target in (EXTRACTED, DESTINATION):
        if not target.resolve().is_relative_to(AREA.resolve()):
            raise ValueError("Stage path escaped the isolated source workspace")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    EXTRACTED.mkdir(parents=True, exist_ok=True)
    existing = [p for p in EXTRACTED.rglob("*") if p.is_file()]
    if any(p.relative_to(EXTRACTED).as_posix() not in names for p in existing):
        raise ValueError("Unowned file in the extraction directory")
    command = [TAR, "-xf", str(ARCHIVE), "-C", str(EXTRACTED)]
    with (EVIDENCE / "cef-stage-extract.log").open("a", encoding="utf-8") as log:
        log.write(json.dumps({"argv": command, "archive_sha256": SHA256}) + "\n")
        log.flush()
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        log.write("EXIT_CODE=" + str(result.returncode) + "\n")
    result.check_returncode()
    files = []
    relative_outputs = set()
    for name in names:
        source = EXTRACTED / name
        if source.is_symlink() or not source.is_file() or not source.resolve().is_relative_to(EXTRACTED.resolve()):
            raise ValueError("Unsafe extracted file")
        relative = PurePosixPath(*PurePosixPath(name).parts[2:]).as_posix()
        if relative in relative_outputs:
            raise ValueError("Release/Resources filename collision")
        relative_outputs.add(relative)
        destination = DESTINATION / relative
        if not destination.resolve().is_relative_to(DESTINATION.resolve()):
            raise ValueError("CEF destination member escaped the stage directory")
        source_hash = digest(source)
        if destination.exists() and (not destination.is_file() or digest(destination) != source_hash):
            raise ValueError("Refusing to overwrite a differing CEF stage file: " + str(destination))
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists():
            shutil.copy2(source, destination)
        if digest(destination) != source_hash:
            raise ValueError("Staged CEF checksum mismatch")
        files.append({"path": relative, "archive_member": name,
                      "bytes": destination.stat().st_size, "sha256": source_hash})
    result = {"archive": str(ARCHIVE), "archive_sha256": SHA256,
              "source_commit": "382ce03a22c5bfb98848b1483a6716b81ee4aeb3",
              "destination": str(DESTINATION), "members_including_directories": len(listing),
              "files": files, "programs_executed": False}
    (EVIDENCE / "cef-stage.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"destination": str(DESTINATION), "files": len(files), "sha256": SHA256}))


if __name__ == "__main__":
    main()
