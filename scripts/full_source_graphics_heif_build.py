"""Build the pinned ONLYOFFICE HEIF dependencies locally; never installs tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "build/full-source"
ALIAS = Path(r"C:\Users\18268\AppData\Local\Temp\oo10-full-src")
HEIF = Path(r"C:\Users\18268\AppData\Local\Temp\oo-graphics-heif-src")
EVIDENCE = AREA / "graphics-dependency-evidence"
CMAKE = r"D:\Qt\Tools\CMake_64\bin\cmake.exe"
NINJA = r"D:\Qt\Tools\Ninja\ninja.exe"
ENV = os.environ.copy()
for key in ("CL", "_CL_", "CFLAGS", "CXXFLAGS", "CC", "CXX", "LDFLAGS"):
    ENV.pop(key, None)


def run(name, args, cwd):
    print(name, flush=True)
    with (EVIDENCE / (name + ".log")).open("a", encoding="utf-8") as log:
        log.write(json.dumps({"argv": [str(a) for a in args], "cwd": str(cwd),
                              "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}) + "\n")
        log.flush()
        result = subprocess.run([str(a) for a in args], cwd=cwd, env=ENV,
                                stdout=log, stderr=subprocess.STDOUT)
        log.write("\nEXIT_CODE=" + str(result.returncode) + "\n")
    if result.returncode:
        print((EVIDENCE / (name + ".log")).read_text(encoding="utf-8", errors="replace")[-14000:], flush=True)
        raise SystemExit(result.returncode)


def build_dir(name):
    return HEIF / name / "build/win_64/release"


def smoke():
    output = EVIDENCE / "heif-source-roundtrip.exe"
    run("heif-smoke-compile", ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/O2", "/MD", "/utf-8",
        "/DLIBHEIF_STATIC_BUILD", "/DLIBDE265_STATIC_BUILD",
        "/I" + str(build_dir("x265")),
        "/I" + str(build_dir("libde265")),
        "/I" + str(HEIF / "libheif/libheif/api"), "/I" + str(build_dir("libheif")),
        ROOT / "scripts/full_source_graphics_heif_smoke.cpp",
        "/Fo" + str(EVIDENCE / "heif-source-roundtrip.obj"), "/Fe" + str(output),
        "/link", build_dir("libheif") / "libheif/Release/heif.lib",
        build_dir("libde265") / "libde265/Release/libde265.lib",
        build_dir("x265") / "Release/x265-static.lib"], EVIDENCE)
    try:
        run("heif-smoke-run", [output, EVIDENCE / "lossless-roundtrip.heic"], EVIDENCE)
    finally:
        lines = (EVIDENCE / "heif-smoke-run.log").read_text(encoding="utf-8", errors="replace").splitlines()
        results = [json.loads(line) for line in lines if line.startswith('{"ok":')]
        if results:
            result = results[-1]
            artifact = EVIDENCE / "lossless-roundtrip.heic"
            result["artifact_sha256"] = hashlib.sha256(artifact.read_bytes()).hexdigest()
            (EVIDENCE / "heif-smoke-result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def configure(name, source, target, output, extra):
    build = build_dir(name)
    args = [CMAKE, "--debug-trycompile", "-S", source, "-B", build, "-G", "Ninja",
            "-DCMAKE_MAKE_PROGRAM=" + NINJA,
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",
            "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=" + output.as_posix(),
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=" + output.as_posix()] + extra
    run(name + "-configure", args, source)
    run(name + "-build", [CMAKE, "--build", build, "--target", target, "--parallel", "3"], source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=("x265", "libde265", "libheif"))
    parser.add_argument("--smoke-only", action="store_true")
    args = parser.parse_args()
    if ALIAS.resolve() != AREA.resolve():
        raise RuntimeError("Unexpected isolated workspace alias")
    if HEIF.resolve() != (AREA / "DesktopEditors/core/Common/3dParty/heif").resolve():
        raise RuntimeError("Unexpected isolated HEIF source alias")
    if ENV.get("VCTOOLSVERSION", "").rstrip("\\/") != "14.44.35207":
        raise RuntimeError("Run via the supplied .cmd to select MSVC 14.44.35207")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    if args.smoke_only:
        smoke()
        return
    if not args.only or args.only == "x265":
        configure("x265", HEIF / "x265/source", "x265-static", build_dir("x265") / "Release", [
            "-DENABLE_CLI=OFF", "-DENABLE_SHARED=OFF", "-DENABLE_ASSEMBLY=OFF",
            "-DENABLE_LIBNUMA=OFF", "-DSTATIC_LINK_CRT=OFF", "-DENABLE_TESTS=OFF"])
        shutil.copy2(HEIF / "x265/source/x265.h", build_dir("x265") / "x265.h")
    if not args.only or args.only == "libde265":
        configure("libde265", HEIF / "libde265", "de265", build_dir("libde265") / "libde265/Release", [
            "-DBUILD_SHARED_LIBS=OFF", "-DENABLE_SDL=OFF", "-DENABLE_DECODER=OFF",
            "-DENABLE_ENCODER=OFF"])
        shutil.copy2(HEIF / "libde265/libde265/de265.h", build_dir("libde265") / "libde265/de265.h")
    if not args.only or args.only == "libheif":
        configure("libheif", HEIF / "libheif", "heif", build_dir("libheif") / "libheif/Release", [
            "--preset=release-noplugins", "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_TESTING=OFF",
            "-DWITH_LIBSHARPYUV=OFF", "-DWITH_AOM_DECODER=OFF", "-DWITH_AOM_ENCODER=OFF",
            "-DWITH_GDK_PIXBUF=OFF", "-DWITH_GNOME=OFF", "-DWITH_EXAMPLES=OFF",
            "-DWITH_EXAMPLE_HEIF_VIEW=OFF", "-DWITH_X265=ON", "-DWITH_LIBDE265=ON",
            "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /W3 /utf-8 /DLIBDE265_STATIC_BUILD",
            "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /W3 /EHsc /utf-8 /DLIBDE265_STATIC_BUILD",
            "-DLIBDE265_INCLUDE_DIR=" + build_dir("libde265").as_posix(),
            "-DX265_INCLUDE_DIR=" + build_dir("x265").as_posix(),
            "-DLIBDE265_LIBRARY=" + (build_dir("libde265") / "libde265/Release/libde265.lib").as_posix(),
            "-DX265_LIBRARY=" + (build_dir("x265") / "Release/x265-static.lib").as_posix()])
    outputs = [build_dir("x265") / "Release/x265-static.lib",
               build_dir("libde265") / "libde265/Release/libde265.lib",
               build_dir("libheif") / "libheif/Release/heif.lib"]
    manifest = {"compiler": ENV["VCTOOLSVERSION"], "configuration": "Release /MD x64",
                "x265_assembly": False, "artifacts": []}
    for output in outputs:
        if output.is_file():
            manifest["artifacts"].append({"path": str(output.resolve()), "bytes": output.stat().st_size,
                                          "sha256": hashlib.sha256(output.read_bytes()).hexdigest()})
    (EVIDENCE / "heif-build-result.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2), flush=True)
    if len(manifest["artifacts"]) == 3:
        smoke()


if __name__ == "__main__":
    main()
