#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Assemble a candidate runtime that uses the source-rebuilt SDKJS WASM engines.

Copies the accepted build/full-source-run (without its openoffice-profile user state) to a new
directory and replaces only the SDKJS engine files with the rebuilt ones from build/wasm-source/out.
Before replacing, every original engine file must be byte-identical to the pinned official SDKJS
deploy, so the candidate differs from the accepted runtime exactly by the listed files. The copy is
made in a sibling staging directory, verified file by file, then renamed. Nothing in the accepted
runtime, SDKJS or upstream is modified; an existing target is never overwritten.

Usage:
    python -I scripts/stage_wasm_candidate_runtime.py --target build/full-source-run-wasm-candidate
"""

import argparse
import datetime
import hashlib
import json
import os
import shutil
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build" / "full-source-run"
OFFICIAL_SDKJS = ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "deploy" / "sdkjs"
OUT = ROOT / "build" / "wasm-source" / "out"
EVIDENCE = ROOT / "evidence" / "wasm-source"
SKIP_TOP_LEVEL = {"openoffice-profile"}  # user state of the accepted runtime; the candidate starts fresh

# (SDKJS-relative directory, rebuilt deploy dir, files to replace, official files to remove, build evidence)
ENGINES = [
    ("common/libfont/engine", OUT / "fonts" / "pass2" / "deploy", ["fonts.js", "fonts_ie.js", "fonts.wasm"], [],
     "fonts-build-pass2.json", "fonts-functional-pass5.json"),
    ("common/zlib/engine", OUT / "zlib" / "pass8" / "deploy", ["zlib.js", "zlib_ie.js", "zlib.wasm"], [],
     "zlib-build-pass8.json", "zlib-functional-pass3.json"),
    ("common/spell/spell", OUT / "spell" / "pass2" / "deploy", ["spell.js", "spell_ie.js", "spell.wasm"],
     ["spell.js.mem"], "spell-build-pass2.json", "spell-functional-pass2.json"),
    ("common/hash/hash", OUT / "engine" / "pass1" / "deploy", ["engine.js", "engine_ie.js", "engine.wasm"], [],
     "engine-build-pass1.json", "hash-functional-pass1.json"),
    ("pdf/src/engine", OUT / "drawingfile" / "pass6" / "deploy", ["drawingfile.js", "drawingfile_ie.js", "drawingfile.wasm"], [],
     "drawingfile-build-pass6.json", "drawingfile-functional-pass2.json"),
]
KEPT_OFFICIAL = {
    "common/libfont/engine/fonts_native.js": "native bridge JS generated from a template, not part of the WASM rebuild",
    "pdf/src/engine/drawingfile_native.js": "native bridge JS without Emscripten glue; current template source is not public",
}
REMOVED_REASON = {"spell.js.mem": "memory file of the official 2021 asm.js build; the rebuilt spell_ie.js embeds its "
                                  "memory (--memory-init-file 0) and does not reference it"}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--target", type=Path, required=True)
    args = parser.parse_args()
    target = args.target if args.target.is_absolute() else ROOT / args.target
    if target.exists():
        sys.exit(f"{target} exists; refusing to overwrite")
    report_path = EVIDENCE / f"candidate-runtime-{target.name}.json"
    if report_path.exists():
        sys.exit(f"{report_path} exists")

    replacements, removals = [], []
    for rel_dir, deploy, files, remove, build_ev, test_ev in ENGINES:
        for name in files + remove:
            runtime_file = SOURCE / "editors" / "sdkjs" / rel_dir / name
            official = OFFICIAL_SDKJS / rel_dir / name
            if sha256(runtime_file) != sha256(official):
                sys.exit(f"{runtime_file} is not the official SDKJS file; refusing to assume what it replaces")
        for name in files:
            if not (deploy / name).is_file():
                sys.exit(f"rebuilt {deploy / name} missing")
            for ev in (build_ev, test_ev):
                if not (EVIDENCE / ev).is_file():
                    sys.exit(f"evidence {ev} missing")

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

    for rel_dir, deploy, files, remove, build_ev, test_ev in ENGINES:
        for name in files:
            dst = staging / "editors" / "sdkjs" / rel_dir / name
            old = sha256(dst)
            shutil.copy2(deploy / name, dst)
            new = sha256(dst)
            if new != sha256(deploy / name):
                sys.exit(f"copy verification failed for {dst}")
            replacements.append({"path": f"editors/sdkjs/{rel_dir}/{name}", "official_sha256": old,
                                 "rebuilt_sha256": new, "bytes": dst.stat().st_size,
                                 "rebuilt_from": str(deploy / name), "build_evidence": build_ev,
                                 "test_evidence": test_ev})
        for name in remove:
            dst = staging / "editors" / "sdkjs" / rel_dir / name
            removals.append({"path": f"editors/sdkjs/{rel_dir}/{name}", "official_sha256": sha256(dst),
                             "reason": REMOVED_REASON[name]})
            dst.unlink()

    # verify: every file not replaced/removed is byte-identical to the accepted runtime
    changed = {r["path"] for r in replacements} | {r["path"] for r in removals}
    verified = mismatches = 0
    for dirpath, _, filenames in os.walk(staging):
        for name in filenames:
            rel = (Path(dirpath) / name).relative_to(staging).as_posix()
            if rel in changed:
                continue
            if sha256(Path(dirpath) / name) != sha256(SOURCE / rel):
                mismatches += 1
            verified += 1
    if mismatches or verified + len(replacements) + len(removals) != copied:
        sys.exit(f"verification failed: {mismatches} mismatches, {verified} verified, {copied} copied; "
                 f"staging kept at {staging}")

    marker = {
        "kind": "open-office-wasm-candidate-runtime",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source_runtime": str(SOURCE),
        "source_stage_manifest_sha256": sha256(SOURCE / "openoffice-full-source-stage.json"),
        "note": "openoffice-full-source-stage.json is copied unchanged and describes the source runtime; this file "
                "lists the only differences",
        "skipped_top_level": sorted(SKIP_TOP_LEVEL),
        "files_copied": copied, "files_verified_identical": verified,
        "replaced": replacements, "removed": removals, "kept_official": KEPT_OFFICIAL,
        "docx_roundtrip_verified": False,
    }
    (staging / "openoffice-wasm-candidate.json").write_text(json.dumps(marker, ensure_ascii=False, indent=2) + "\n",
                                                           encoding="utf-8")
    staging.rename(target)
    report = dict(marker, target=str(target), marker_sha256=sha256(target / "openoffice-wasm-candidate.json"))
    with open(report_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"target": str(target), "copied": copied, "verified_identical": verified,
                      "replaced": len(replacements), "removed": len(removals), "report": str(report_path)},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
