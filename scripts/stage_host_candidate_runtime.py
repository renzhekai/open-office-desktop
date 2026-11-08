#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Assemble a candidate runtime that differs from the default entry only by a newly built host.

Copies the WASM candidate runtime build/full-source-run-wasm-candidate (the default entry when this
script was written; since 2026-11-06 the default is the host candidate it produced) without its
openoffice-profile user state to a new directory and replaces only OpenOffice.exe with build/full-source-host/bin/OpenOffice.exe
from a passing build_full_source_host.cmd run. The copy is made in a sibling staging directory,
verified file by file, then renamed. Nothing in the source runtime is modified; an existing target is
never overwritten. The marker openoffice-host-candidate.json records the replaced host and the build
and test logs it came from.

Usage:
    python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-host-saveas --build-label new-saveas-2
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build" / "full-source-run-wasm-candidate"
HOST = ROOT / "build" / "full-source-host" / "bin" / "OpenOffice.exe"
HOST_EVIDENCE = ROOT / "build" / "full-source" / "host-evidence"
TEST_RESULTS = [ROOT / "build" / "full-source-host" / name
                for name in ("engine-contract-results.txt", "native-ui-results.txt", "assistant-results.txt")]
EVIDENCE = ROOT / "evidence" / "host-candidate"
SKIP_TOP_LEVEL = {"openoffice-profile"}  # user state of the source runtime; the candidate starts fresh
MARKER = "openoffice-host-candidate.json"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--build-label", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9-]+", args.build_label):
        parser.error("build label must contain lowercase alphanumeric/hyphen")
    target = args.target if args.target.is_absolute() else ROOT / args.target
    if target.exists():
        sys.exit(f"{target} exists; refusing to overwrite")
    if (SOURCE / MARKER).exists():
        sys.exit("the source runtime already is a host candidate; stage from the default entry only")
    report_path = EVIDENCE / f"candidate-runtime-{target.name}.json"
    if report_path.exists():
        sys.exit(f"{report_path} exists")

    logs = HOST_EVIDENCE / args.build_label
    ctest_log = logs / "ctest.log"
    if not ctest_log.is_file() or "100% tests passed" not in ctest_log.read_text(encoding="utf-8", errors="replace"):
        sys.exit(f"{ctest_log} does not record a passing CTest run")
    build_log = logs / "build.log"
    if not build_log.is_file() or HOST.stat().st_mtime < build_log.stat().st_mtime - 600:
        sys.exit("the host executable is not from the named build")
    for results in TEST_RESULTS:
        if not results.is_file() or ", 0 failed," not in results.read_text(encoding="utf-8", errors="replace"):
            sys.exit(f"{results} does not record a passing suite")

    staging = target.parent / f".{target.name}.staging-{uuid.uuid4().hex}"
    staging.mkdir(parents=True)
    copied = 0
    for dirpath, dirnames, filenames in os.walk(SOURCE):
        rel = Path(dirpath).relative_to(SOURCE)
        if rel == Path("."):
            dirnames[:] = [d for d in dirnames if d not in SKIP_TOP_LEVEL]
        (staging / rel).mkdir(parents=True, exist_ok=True)
        for name in filenames:
            shutil.copy2(Path(dirpath) / name, staging / rel / name)
            copied += 1

    destination = staging / "OpenOffice.exe"
    previous = sha256(destination)
    shutil.copy2(HOST, destination)
    host_sha = sha256(destination)
    if host_sha != sha256(HOST):
        sys.exit(f"copy verification failed; staging kept at {staging}")
    if host_sha == previous:
        sys.exit(f"the built host equals the source runtime host; staging kept at {staging}")

    verified = mismatches = 0
    for dirpath, _, filenames in os.walk(staging):
        for name in filenames:
            rel = (Path(dirpath) / name).relative_to(staging).as_posix()
            if rel == "OpenOffice.exe":
                continue
            if sha256(Path(dirpath) / name) != sha256(SOURCE / rel):
                mismatches += 1
            verified += 1
    if mismatches or verified + 1 != copied:
        sys.exit(f"verification failed: {mismatches} mismatches, {verified} verified, {copied} copied; "
                 f"staging kept at {staging}")

    marker = {
        "kind": "open-office-host-candidate-runtime",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source_runtime": str(SOURCE),
        "source_wasm_candidate_marker_sha256": sha256(SOURCE / "openoffice-wasm-candidate.json"),
        "note": "openoffice-full-source-stage.json and openoffice-wasm-candidate.json are copied unchanged and "
                "describe the source runtime; this file lists the only difference",
        "skipped_top_level": sorted(SKIP_TOP_LEVEL),
        "files_copied": copied, "files_verified_identical": verified,
        "host": {"path": "OpenOffice.exe", "previous_sha256": previous, "sha256": host_sha,
                 "bytes": destination.stat().st_size, "built_from": str(HOST),
                 "build_label": args.build_label,
                 "build_log_sha256": sha256(build_log), "ctest_log_sha256": sha256(ctest_log),
                 "test_results_sha256": {p.name: sha256(p) for p in TEST_RESULTS}},
        "gui_regression_verified": False,
    }
    (staging / MARKER).write_text(json.dumps(marker, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    staging.rename(target)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    report = dict(marker, target=str(target), marker_sha256=sha256(target / MARKER))
    with open(report_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"target": str(target), "copied": copied, "verified_identical": verified,
                      "host_sha256": host_sha, "report": str(report_path)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
