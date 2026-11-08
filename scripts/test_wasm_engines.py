#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Differential functional test for the rebuilt zlib, spell and hash SDKJS engines.

Runs scripts/wasm_engine_harness.cjs on the official and the rebuilt engine, for both the
WebAssembly (<name>.js) and asm.js (<name>_ie.js) variants, with the same inputs, and compares
the results. Independent checks are added where Python has its own implementation:
zip archives (zipfile), message digests and the Office password hash (hashlib).

Usage:
    python -I scripts/test_wasm_engines.py --module <zlib|spell|hash> --rebuilt <deploy dir> --label <new>
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
SDKJS = ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "deploy" / "sdkjs"
HARNESS = ROOT / "scripts" / "wasm_engine_harness.cjs"
RUNTIME = ROOT / "build" / "full-source-run"
CORE_EXAMPLES = ROOT / "build" / "full-source" / "DesktopEditors" / "core" / "OOXML" / "Binary" / "MathEquation" / "Examples"
OFFICE_CLIPART = Path("C:/Program Files/Microsoft Office/root/CLIPART/PUB60COR")

MODULES = {
    "zlib": {"official": SDKJS / "common" / "zlib" / "engine", "name": "zlib"},
    "spell": {"official": SDKJS / "common" / "spell" / "spell", "name": "spell"},
    "hash": {"official": SDKJS / "common" / "hash" / "hash", "name": "engine"},
    "drawingfile": {"official": SDKJS / "pdf" / "src" / "engine", "name": "drawingfile"},
}
PDF_INPUTS = ROOT / "build" / "wasm-source" / "test" / "pdf-inputs"  # from scripts/make_test_pdfs.py


def strip_info(value):
    """Drop fields that may legitimately differ between runs (e.g. dates/IDs inside generated PDFs)."""
    if isinstance(value, dict):
        return {k: strip_info(v) for k, v in value.items() if not k.startswith("info_")}
    if isinstance(value, list):
        return [strip_info(v) for v in value]
    return value
# CHash::HashAlgs order -> hashlib name (None: no independent implementation available here).
HASH_ALGS = [("md2", None), ("md4", None), ("md5", "md5"), ("rmd160", "ripemd160"), ("sha1", "sha1"),
             ("sha256", "sha256"), ("sha384", "sha384"), ("sha512", "sha512"), ("whirlpool", None)]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def zip_entries():
    rng = random.Random(20261103)
    return [
        ("word/document.xml", "<w:document>开放办公 WASM 测试 – zip round trip</w:document>".encode("utf-8") * 50),
        ("media/random.bin", bytes(rng.randrange(256) for _ in range(70000))),
        ("docProps/空文件.txt", b""),
        ("deep/nested/path/data.txt", b"0123456789" * 1000),
    ]


def build_inputs(module, work):
    files = []
    if module == "zlib":
        fixtures = [ROOT / "fixtures" / "generated" / n for n in ("text-table.docx", "values-formula.xlsx", "text-shape.pptx")]
        fixtures.append(ROOT / "fixtures" / "results" / "full-source-docx-roundtrip.docx")
        images = [RUNTIME / "editors/web-apps/apps/common/main/resources/help/images/symbols.png",
                  RUNTIME / "editors/web-apps/apps/common/main/resources/help/fr/images/charteditor.png",
                  RUNTIME / "editors/web-apps/apps/documenteditor/main/resources/help/fr/images/cellvalueformula.gif",
                  Path("C:/Windows/Web/4K/Wallpaper/Windows/img0_1920x1200.jpg"),
                  CORE_EXAMPLES / "image1.wmf", CORE_EXAMPLES / "image10.wmf",
                  OFFICE_CLIPART / "AN00010_.WMF", OFFICE_CLIPART / "AN00790_.WMF"]
        images = [p for p in images if p.is_file()]
        inputs = {"zip_entries": [{"path": p, "base64": base64.b64encode(d).decode("ascii")} for p, d in zip_entries()],
                  "zip_fixtures": [str(p) for p in fixtures], "images": [str(p) for p in images],
                  "encode_formats": [4, 3, 24]}
        files = fixtures + images
    elif module == "spell":
        d = RUNTIME / "dictionaries"
        words = {
            "en_US": ["hello", "world", "documentation", "international", "helo", "wrold", "documantation", "recieve"],
            "de_DE": ["Straße", "Rechtschreibung", "Haus", "Hauss", "Rechtschreibbung", "Strase"],
            "ru_RU": ["привет", "электростанция", "превет", "електростанция"],
            "fr_FR": ["bonjour", "français", "bonjur", "francais"],
            "es_ES": ["hola", "español", "ola", "espanol"],
        }
        dictionaries = [{"lang": lang, "aff": str(d / lang / f"{lang}.aff"), "dic": str(d / lang / f"{lang}.dic"),
                         "words": w} for lang, w in words.items()]
        inputs = {"dictionaries": dictionaries}
        files = [Path(x[k]) for x in dictionaries for k in ("aff", "dic")]
    elif module == "drawingfile":
        pdfs = [(PDF_INPUTS / "document.pdf", 2), (PDF_INPUTS / "form.pdf", 1)]
        font = RUNTIME / "fonts" / "OpenSans-Regular.ttf"
        image = Path("C:/Windows/Web/4K/Wallpaper/Windows/img0_1920x1200.jpg")
        inputs = {"pdfs": [{"path": str(p), "pages": n} for p, n in pdfs], "render_sizes": [[612, 792], [153, 198]],
                  "storage_font": str(font), "storage_image": str(image)}
        files = [p for p, _ in pdfs] + [font, image]
    else:
        rng = random.Random(42)
        data = [("empty", b""), ("abc", b"abc"), ("fox", b"The quick brown fox jumps over the lazy dog"),
                ("million_a", b"a" * 1_000_000), ("random_70000", bytes(rng.randrange(256) for _ in range(70000))),
                ("utf8_chinese", "开放办公软件，所有功能免费。".encode("utf-8"))]
        salt = base64.b64encode(bytes(range(16))).decode("ascii")
        hash2 = [{"password": "密码Password1!", "salt_base64": salt, "spin": spin, "alg": alg}
                 for alg in range(9) for spin in (0, 1, 1000)]
        hash2 += [{"password": "Office", "salt_base64": salt, "spin": 100000, "alg": alg} for alg in (4, 7)]
        inputs = {"hash_inputs": [{"name": n, "base64": base64.b64encode(b).decode("ascii")} for n, b in data],
                  "hash2_inputs": hash2}
        inputs["_raw"] = data
    fingerprints = {str(p): {"bytes": p.stat().st_size, "sha256": sha256(p)} for p in files}
    return inputs, fingerprints


def run_harness(module, engine, inputs, work, tag):
    case = {k: v for k, v in inputs.items() if not k.startswith("_")}
    if module == "zlib":
        case["zip_output"] = str(work / f"{tag}-created.zip")
        svg_dir = work / f"{tag}-svg"
        svg_dir.mkdir()
        case["svg_dir"] = str(svg_dir)
    inputs_path, output_path = work / f"{tag}-inputs.json", work / f"{tag}-result.json"
    inputs_path.write_text(json.dumps(case, ensure_ascii=False), encoding="utf-8")
    proc = subprocess.run(["node", str(HARNESS), module, str(engine), str(inputs_path), str(output_path)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600)
    run = {"engine": str(engine), "engine_sha256": sha256(engine), "exit": proc.returncode, "stderr": proc.stderr[-3000:]}
    data = json.loads(output_path.read_text(encoding="utf-8")) if proc.returncode == 0 else None
    return run, data


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


def all_differences(a, b, prefix=""):
    """Leaf paths that differ (for reporting), capped."""
    out = []
    def walk(x, y, where):
        if len(out) >= 50:
            return
        if isinstance(x, dict) and isinstance(y, dict):
            for key in sorted(set(x) | set(y)):
                walk(x.get(key), y.get(key), f"{where}.{key}")
        elif isinstance(x, list) and isinstance(y, list) and len(x) == len(y):
            for index, (p, q) in enumerate(zip(x, y)):
                walk(p, q, f"{where}[{index}]")
        elif x != y:
            out.append(where)
    walk(a, b, prefix)
    return out


def independent_checks(module, inputs, result, work, tag):
    checks = {}
    if module == "zlib":
        entries = zip_entries()
        with zipfile.ZipFile(work / f"{tag}-created.zip") as archive:
            names = {}
            for info in archive.infolist():
                name = info.filename if info.flag_bits & 0x800 else info.filename.encode("cp437").decode("utf-8")
                names[name] = info
            checks["created_zip_crc"] = archive.testzip() is None
            checks["created_zip_content"] = all(n in names and archive.read(names[n]) == d for n, d in entries)
        expected = {n: hashlib.sha256(d).hexdigest() for n, d in entries if d}
        checks["created_reread_matches_inputs"] = result["zip"]["reread_created"] == expected
        fixture_ok = True
        for fixture in result["zip"]["fixtures"]:
            path = next(Path(p) for p in inputs["zip_fixtures"] if Path(p).name == fixture["file"])
            with zipfile.ZipFile(path) as archive:
                want = {i.filename: hashlib.sha256(archive.read(i)).hexdigest() for i in archive.infolist() if i.file_size}
            fixture_ok = fixture_ok and fixture["files"] == want
        checks["fixtures_match_python_zipfile"] = fixture_ok
    elif module == "hash":
        raw = dict(inputs["_raw"])
        ok, compared = True, 0
        for item in result["hash"]:
            for alg, (_, py) in enumerate(HASH_ALGS):
                if py:
                    compared += 1
                    ok = ok and item["digests"][str(alg)] == hashlib.new(py, raw[item["name"]]).hexdigest()
        checks["hash_matches_hashlib"] = ok
        checks["hash_values_compared"] = compared
        ok2, compared2 = True, 0
        for item in result["hash2"]:
            py = HASH_ALGS[item["alg"]][1]
            if not py:
                continue
            h = hashlib.new(py, base64.b64decode(item["salt_base64"]) + item["password"].encode("utf-16-le")).digest()
            for i in range(item["spin"]):
                h = hashlib.new(py, h + i.to_bytes(4, "little")).digest()
            compared2 += 1
            ok2 = ok2 and item["digest"] == h.hex()
        checks["hash2_matches_python_reimplementation"] = ok2
        checks["hash2_values_compared"] = compared2
    elif module == "drawingfile":
        docs = result["documents"]
        checks["documents_open_without_error"] = all(d["open"].get("error") == 0 for d in docs)
        generated = [d[k] for d in docs for k in ("split_first_page", "build_pdf")]
        checks["split_and_build_reopen_as_pdf"] = all(g.get("starts_pdf") and g.get("reopen_error") == 0 for g in generated)
        st = result["storages"]
        checks["font_storage_set_and_clear"] = (st["font_before"], st["font_after_set"], st["font_after_clear"]) == (0, 1, 0)
        checks["image_storage_set_and_clear"] = (st["image_before"], st["image_after_set"], st["image_after_clear"]) == (0, 1, 0)
        checks["pages_rendered"] = sum(1 for d in docs for pg in d["pages"] for k, v in pg.items()
                                       if k.startswith("pixmap_") and isinstance(v, str))
    else:
        expectations = {"hello": 1, "world": 1, "documentation": 1, "international": 1, "helo": 0, "wrold": 0,
                        "documantation": 0, "recieve": 0, "Straße": 1, "Rechtschreibung": 1, "Haus": 1, "Hauss": 0,
                        "Rechtschreibbung": 0, "привет": 1, "электростанция": 1, "превет": 0, "електростанция": 0,
                        "bonjour": 1, "français": 1, "bonjur": 0, "hola": 1, "español": 1}
        wrong = [w for d in result["dictionaries"] for w, r in d["words"].items()
                 if w in expectations and r["spell"] != expectations[w]]
        checks["known_words_classified_as_expected"] = not wrong
        checks["unexpected_classifications"] = wrong
        checks["misspellings_with_suggestions"] = sum(1 for d in result["dictionaries"] for r in d["words"].values()
                                                      if r["spell"] != 1 and r["suggestions"])
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--module", choices=sorted(MODULES), required=True)
    parser.add_argument("--rebuilt", type=Path, required=True)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    config = MODULES[args.module]
    rebuilt = args.rebuilt if args.rebuilt.is_absolute() else ROOT / args.rebuilt
    work = ROOT / "build" / "wasm-source" / "test" / args.module / args.label
    evidence = ROOT / "evidence" / "wasm-source" / f"{args.module}-functional-{args.label}.json"
    if work.exists() or evidence.exists():
        sys.exit(f"label {args.label} already used")
    work.mkdir(parents=True)
    inputs, fingerprints = build_inputs(args.module, work)

    report = {"module": args.module, "label": args.label,
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "official_dir": str(config["official"]), "rebuilt_dir": str(rebuilt), "inputs": fingerprints,
              "harness": {"path": str(HARNESS), "sha256": sha256(HARNESS)},
              "node": subprocess.run(["node", "--version"], capture_output=True, text=True).stdout.strip(),
              "variants": {}}
    all_ok = True
    for variant, suffix in (("wasm", ""), ("asmjs", "_ie")):
        filename = f"{config['name']}{suffix}.js"
        runs, results = {}, {}
        for side, directory in (("official", config["official"]), ("rebuilt", rebuilt)):
            runs[side], results[side] = run_harness(args.module, directory / filename, inputs, work, f"{variant}-{side}")
        entry = {"runs": runs}
        if results["official"] and results["rebuilt"]:
            official = strip_info({k: v for k, v in results["official"].items() if k != "engine"})
            rebuilt_result = strip_info({k: v for k, v in results["rebuilt"].items() if k != "engine"})
            if args.module == "zlib":
                official["zip"] = {k: v for k, v in official["zip"].items() if k != "created_bytes"}
                rebuilt_result["zip"] = {k: v for k, v in rebuilt_result["zip"].items() if k != "created_bytes"}
            diff = first_difference(official, rebuilt_result)
            entry["equal_to_official"] = diff is None
            entry["differences"] = all_differences(official, rebuilt_result)
            entry["independent"] = {side: independent_checks(args.module, inputs, results[side], work, f"{variant}-{side}")
                                    for side in ("official", "rebuilt")}
            independent_ok = all(v for c in entry["independent"].values() for k, v in c.items()
                                 if isinstance(v, bool))
            entry["passed"] = entry["equal_to_official"] and independent_ok
        else:
            entry["passed"] = False
        all_ok = all_ok and entry["passed"]
        report["variants"][variant] = entry
    report["all_passed"] = all_ok
    report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    evidence.parent.mkdir(parents=True, exist_ok=True)
    with open(evidence, "x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"all_passed": all_ok, "variants": {
        v: {"passed": e.get("passed"), "equal_to_official": e.get("equal_to_official"),
            "differences": e.get("differences", [])[:15], "independent": e.get("independent"),
            "exits": {s: e["runs"][s]["exit"] for s in e["runs"]}} for v, e in report["variants"].items()}},
        ensure_ascii=False, indent=1))
    for v, e in report["variants"].items():
        for s, r in e["runs"].items():
            if r["exit"] != 0:
                print(f"--- {v} {s} stderr:\n{r['stderr']}")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
