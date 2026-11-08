"""Compile Qt 6 wrappers against this run's source-built native libraries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r"C:\Users\18268\AppData\Local\Temp\oo10-full-src\DesktopEditors")
BUILD = ROOT / "build/full-source-qt6"
EVIDENCE = ROOT / "build/full-source/desktop-build-evidence"
IMPORTS = SOURCE / "core/build/lib/win_64"
CMAKE = r"D:\Qt\Tools\CMake_64\bin\cmake.exe"
NINJA = r"D:\Qt\Tools\Ninja\ninja.exe"
ENV = os.environ.copy()
for key in ("CL", "_CL_", "CFLAGS", "CXXFLAGS", "CC", "CXX", "LDFLAGS"):
    ENV.pop(key, None)


def run(name, arguments):
    print(name, flush=True)
    with (EVIDENCE / (name + ".log")).open("a", encoding="utf-8") as log:
        log.write(json.dumps({"argv": [str(a) for a in arguments], "cwd": str(ROOT),
                              "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}) + "\n")
        log.flush()
        result = subprocess.run([str(a) for a in arguments], cwd=ROOT, env=ENV,
                                stdout=log, stderr=subprocess.STDOUT)
        log.write("\nEXIT_CODE=" + str(result.returncode) + "\n")
    if result.returncode:
        print((EVIDENCE / (name + ".log")).read_text(encoding="utf-8", errors="replace")[-10000:])
        raise SystemExit(result.returncode)


def record(path):
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="Link documentscore too; requires its source-built import library")
    args = parser.parse_args()
    if ENV.get("VCTOOLSVERSION", "").rstrip("\\/") != "14.44.35207":
        raise RuntimeError("Run through the supplied MSVC environment .cmd")
    if SOURCE.resolve() != (ROOT / "build/full-source/DesktopEditors").resolve():
        raise RuntimeError("Unexpected source worktree alias")
    required = ["kernel"] + (["graphics", "ascdocumentscore"] if args.all else [])
    for name in required:
        if not (IMPORTS / (name + ".lib")).is_file():
            raise FileNotFoundError("Missing source-built import library: " + name)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    run("qt6-wrapper-configure", [CMAKE, "-S", ROOT / "tests/integration/native-qt-wrapper", "-B", BUILD,
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",
        "-DCMAKE_MAKE_PROGRAM=" + NINJA, "-DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64",
        "-DONLYOFFICE_SOURCE_ROOT=" + SOURCE.as_posix(),
        "-DONLYOFFICE_NATIVE_IMPORT_DIR=" + IMPORTS.as_posix(),
        "-DONLYOFFICE_VIDEO_ONLY=" + ("OFF" if args.all else "ON")])
    targets = ["openoffice_qt6_videoplayer", "openoffice_qt6_documentscore" if args.all else "qtascdocumentscore_objects"]
    run("qt6-wrapper-build", [CMAKE, "--build", BUILD, "--target"] + targets + ["--parallel", "3"])
    products = [BUILD / "bin/openoffice_qt6_videoplayer.dll", BUILD / "lib/openoffice_qt6_videoplayer.lib"]
    if args.all:
        products += [BUILD / "bin/openoffice_qt6_documentscore.dll", BUILD / "lib/openoffice_qt6_documentscore.lib"]
    receipt = {"source_root": str(SOURCE.resolve()), "build": str(BUILD),
               "compiler_toolset": ENV["VCTOOLSVERSION"], "configuration": "Release /MD x64",
               "qt_version": "6.11.1", "document_wrapper_linked": args.all,
               "native_imports": [record(IMPORTS / (name + ".lib")) for name in required],
               "files": [record(p) for p in products]}
    (EVIDENCE / "qt6-wrapper-build.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
