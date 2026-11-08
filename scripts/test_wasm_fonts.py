#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Differential functional test: official SDKJS fonts engine vs a rebuilt one.

Runs scripts/wasm_fonts_harness.cjs on the official and the rebuilt engine
(both the WebAssembly fonts.js and the asm.js fonts_ie.js variants) with the
same inputs, then compares FreeType metrics/outlines/bitmaps, HarfBuzz shaping,
zip read/write, raster decode/encode and hyphenation results. The archive each
build creates is also checked with Python's zipfile as an independent reader.

Usage:
    python -I scripts/test_wasm_fonts.py --rebuilt build/wasm-source/out/fonts/<label>/deploy --label <new>
"""

import argparse
import base64
import datetime
import hashlib
import json
import random
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OFFICIAL = ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "deploy" / "sdkjs" / "common" / "libfont" / "engine"
HARNESS = ROOT / "scripts" / "wasm_fonts_harness.cjs"
RUNTIME = ROOT / "build" / "full-source-run"
SYSTEM_FONTS = Path("C:/Windows/Fonts")

LATN, HANI, ARAB = 0x4C61746E, 0x48616E69, 0x41726162
LTR, RTL = 4, 5


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build_inputs(work):
    fonts = [
        (RUNTIME / "fonts" / "OpenSans-Regular.ttf", [0]),
        (RUNTIME / "fonts" / "crosextra" / "Carlito-Regular.ttf", [0]),
        (RUNTIME / "fonts" / "caladea" / "caladea-regular.ttf", [0]),
        (RUNTIME / "fonts" / "asana" / "ASANA.ttc", [0]),
        (SYSTEM_FONTS / "arial.ttf", [0]),
        (SYSTEM_FONTS / "NotoSansSC-VF.ttf", [0]),
    ]
    fonts = [(p, faces) for p, faces in fonts if p.is_file()]
    rng = random.Random(20261103)
    zip_entries = [
        ("word/document.xml", "<w:document>开放办公 WASM 测试 – zip round trip</w:document>".encode("utf-8") * 50),
        ("media/random.bin", bytes(rng.randrange(256) for _ in range(70000))),
        ("docProps/空文件.txt", b""),
        ("deep/nested/path/data.txt", b"0123456789" * 1000),
    ]
    images = [
        RUNTIME / "editors/web-apps/apps/common/main/resources/help/images/symbols.png",
        RUNTIME / "editors/web-apps/apps/common/main/resources/help/fr/images/charteditor.png",
        RUNTIME / "editors/web-apps/apps/documenteditor/main/resources/help/fr/images/cellvalueformula.gif",
        Path("C:/Windows/Web/4K/Wallpaper/Windows/img0_1920x1200.jpg"),
    ]
    images = [p for p in images if p.is_file()]
    dictionaries = RUNTIME / "dictionaries"
    inputs = {
        "fonts": [{"path": str(p), "faces": faces} for p, faces in fonts],
        "codepoints": [ord(c) for c in "AaWgfi中文字€йßا☃"] + [0x1F600],
        "glyph_scan": 300,
        "load_flags": [0, 2],
        "shaping": [
            {"font": "OpenSans-Regular.ttf", "text": "office fi ffl AV Wa", "script": LATN, "direction": LTR, "language": "en", "features": 0},
            {"font": "OpenSans-Regular.ttf", "text": "office fi ffl AV Wa", "script": LATN, "direction": LTR, "language": "en", "features": 17},
            {"font": "Carlito-Regular.ttf", "text": "Difficult kerning AVATAR", "script": LATN, "direction": LTR, "language": "en", "features": 17},
            {"font": "NotoSansSC-VF.ttf", "text": "中文排版测试，标点。", "script": HANI, "direction": LTR, "language": "zh", "features": 0},
            {"font": "arial.ttf", "text": "مرحبا بالعالم", "script": ARAB, "direction": RTL, "language": "ar", "features": 0},
        ],
        "zip_entries": [{"path": p, "base64": base64.b64encode(d).decode("ascii")} for p, d in zip_entries],
        "zip_fixture": str(ROOT / "fixtures" / "generated" / "text-table.docx"),
        "images": [str(p) for p in images],
        "encode_formats": [4, 3],
        "hyphen": [
            {"path": str(dictionaries / "en_US" / "hyph_en_US.dic"), "lcid": 1033,
             "words": ["hyphenation", "international", "documentation", "responsibility", "encyclopedia", "characteristically"]},
            {"path": str(dictionaries / "de_DE" / "hyph_de_DE.dic"), "lcid": 1031,
             "words": ["Silbentrennung", "Donaudampfschifffahrt", "Rechtschreibung", "Bundesverfassungsgericht"]},
            {"path": str(dictionaries / "ru_RU" / "hyph_ru_RU.dic"), "lcid": 1049,
             "words": ["переносить", "электростанция", "достопримечательность"]},
        ],
    }
    input_files = [p for p, _ in fonts] + images + [Path(inputs["zip_fixture"])] + [Path(h["path"]) for h in inputs["hyphen"]]
    fingerprints = {str(p): {"bytes": p.stat().st_size, "sha256": sha256(p)} for p in input_files}
    return inputs, zip_entries, fingerprints


def run_harness(engine, inputs, work, tag):
    case_inputs = dict(inputs, zip_output=str(work / f"{tag}-created.zip"))
    inputs_path = work / f"{tag}-inputs.json"
    output_path = work / f"{tag}-result.json"
    inputs_path.write_text(json.dumps(case_inputs, ensure_ascii=False), encoding="utf-8")
    proc = subprocess.run(["node", str(HARNESS), str(engine), str(inputs_path), str(output_path)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600)
    run = {"engine": str(engine), "engine_sha256": sha256(engine), "exit": proc.returncode,
           "stderr": proc.stderr[-3000:], "result": str(output_path)}
    data = json.loads(output_path.read_text(encoding="utf-8")) if proc.returncode == 0 else None
    return run, data


def check_created_zip(path, entries):
    """Read the archive with Python's zipfile. minizip writes UTF-8 names without the
    general-purpose UTF-8 flag (bit 11), so zipfile decodes them as cp437; undo that."""
    with zipfile.ZipFile(path) as archive:
        bad = archive.testzip()
        by_name, utf8_flags = {}, []
        for info in archive.infolist():
            flagged = bool(info.flag_bits & 0x800)
            utf8_flags.append(flagged)
            name = info.filename if flagged else info.filename.encode("cp437").decode("utf-8")
            by_name[name] = info
        content_ok = all(name in by_name and archive.read(by_name[name]) == data for name, data in entries)
    return {"python_zipfile_crc": bad is None, "names": sorted(by_name), "utf8_name_flag_set": utf8_flags,
            "content_matches_inputs": content_ok}


def first_difference(a, b, where=""):
    if type(a) is not type(b):
        return where or "<root>"
    if isinstance(a, dict):
        for key in sorted(set(a) | set(b)):
            if key not in a or key not in b:
                return f"{where}.{key}"
            found = first_difference(a[key], b[key], f"{where}.{key}")
            if found:
                return found
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return f"{where}[len {len(a)} != {len(b)}]"
        for index, (x, y) in enumerate(zip(a, b)):
            found = first_difference(x, y, f"{where}[{index}]")
            if found:
                return found
        return None
    return None if a == b else where


def compare(official, rebuilt):
    sections = {}
    for section in ("freetype", "harfbuzz", "raster", "hyphen"):
        diff = first_difference(official[section], rebuilt[section])
        sections[section] = {"equal": diff is None, "first_difference": diff}
    zip_official = {k: official["zlib"][k] for k in ("reread_created", "fixture")}
    zip_rebuilt = {k: rebuilt["zlib"][k] for k in ("reread_created", "fixture")}
    diff = first_difference(zip_official, zip_rebuilt)
    sections["zlib_read"] = {"equal": diff is None, "first_difference": diff}
    return sections


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--rebuilt", type=Path, required=True)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    rebuilt = args.rebuilt if args.rebuilt.is_absolute() else ROOT / args.rebuilt
    work = ROOT / "build" / "wasm-source" / "test" / "fonts" / args.label
    evidence = ROOT / "evidence" / "wasm-source" / f"fonts-functional-{args.label}.json"
    if work.exists() or evidence.exists():
        sys.exit(f"label {args.label} already used")
    work.mkdir(parents=True)
    inputs, zip_entries, fingerprints = build_inputs(work)

    report = {"label": args.label, "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "official_dir": str(OFFICIAL), "rebuilt_dir": str(rebuilt), "inputs": fingerprints,
              "harness": {"path": str(HARNESS), "sha256": sha256(HARNESS)},
              "node": subprocess.run(["node", "--version"], capture_output=True, text=True).stdout.strip(),
              "variants": {}}
    # Zlib_GetPaths does not list zero-length entries (same in the official engine; the entry is
    # still in the archive, as Python's zipfile confirms), so the engine reread expects only non-empty ones.
    expected_zip = {name: hashlib.sha256(data).hexdigest() for name, data in zip_entries if data}
    report["engine_reread_excludes_empty_entries"] = [name for name, data in zip_entries if not data]
    all_ok = True
    for variant, filename in (("wasm", "fonts.js"), ("asmjs", "fonts_ie.js")):
        runs, results = {}, {}
        for side, directory in (("official", OFFICIAL), ("rebuilt", rebuilt)):
            runs[side], results[side] = run_harness(directory / filename, inputs, work, f"{variant}-{side}")
        entry = {"runs": runs}
        if results["official"] and results["rebuilt"]:
            entry["comparison"] = compare(results["official"], results["rebuilt"])
            for side in ("official", "rebuilt"):
                created = work / f"{variant}-{side}-created.zip"
                check = check_created_zip(created, zip_entries)
                check["reread_matches_inputs"] = results[side]["zlib"]["reread_created"] == expected_zip
                entry[f"{side}_created_zip"] = check
            r = results["rebuilt"]
            entry["coverage"] = {
                "fonts_opened": sum(1 for f in r["freetype"] if f["opened"]),
                "glyph_loads": sum(f.get("glyph_count", 0) for f in r["freetype"]),
                "shaping_runs": len(r["harfbuzz"]),
                "shaped_glyphs": sum(len(h["glyphs"] or []) for h in r["harfbuzz"]),
                "images": len(r["raster"]),
                "images_decoded": sum(1 for i in r["raster"] if i.get("decode_0")),
                "zip_fixture_entries": len(r["zlib"]["fixture"] or {}),
                "hyphen_dictionaries_loaded": sum(1 for h in r["hyphen"] if h["load"] == 0),
                "hyphenated_words": sum(1 for h in r["hyphen"] for marks in h["words"].values() if marks),
            }
            ok = (all(s["equal"] for s in entry["comparison"].values())
                  and all(entry[f"{s}_created_zip"]["python_zipfile_crc"]
                          and entry[f"{s}_created_zip"]["content_matches_inputs"]
                          and entry[f"{s}_created_zip"]["reread_matches_inputs"] for s in ("official", "rebuilt")))
        else:
            ok = False
        entry["passed"] = ok
        all_ok = all_ok and ok
        report["variants"][variant] = entry
    report["all_passed"] = all_ok
    report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    evidence.parent.mkdir(parents=True, exist_ok=True)
    with open(evidence, "x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    summary = {v: {"passed": e["passed"], "comparison": e.get("comparison"), "coverage": e.get("coverage"),
                   "exits": {s: e["runs"][s]["exit"] for s in e["runs"]}}
               for v, e in report["variants"].items()}
    print(json.dumps({"all_passed": all_ok, "variants": summary}, ensure_ascii=False, indent=1))
    for v, e in report["variants"].items():
        for s, r in e["runs"].items():
            if r["exit"] != 0:
                print(f"--- {v} {s} stderr:\n{r['stderr']}")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
