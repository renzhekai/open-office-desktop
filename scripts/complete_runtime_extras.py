#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Adds to an assembled runtime what the official desktop packaging adds and ours did not.

1. Presentation themes. The official deploy runs converter/allthemesgen, which turns
   editors/sdkjs/slide/themes/src/*.pptx into themes.js, theme1..N (theme.bin, thumbnails)
   and editors/sdkjs/common/Images/themes_thumbnail*.png. Without them the presentation
   editor cannot load its themes. The runtime's own allfontsgen and allthemesgen (built from
   source) are used; their scratch output stays outside the runtime.
2. Chinese strings. The v10.0.0 sources' zh.json lack strings that en.json has (107 in the
   presentation editor); a view that reads a missing one (the View tab's tipOutlineView)
   throws, and the presentation editor then turns read-only ("使用文档时出错"). Missing keys
   are filled with the English text, in every editor's main/locale/zh.json.

Nothing that is there is changed except those zh.json files. A report of every file added
or changed (with SHA-256) goes to evidence/host-candidate/runtime-extras-<runtime>.json.

Usage:
    python -I scripts/complete_runtime_extras.py build/full-source-run-builtin-ai-r18
"""

import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "evidence" / "host-candidate"
SCRATCH = ROOT / "build" / "runtime-extras"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, cwd, log):
    with open(log, "wb") as out:
        result = subprocess.run(command, cwd=cwd, stdout=out, stderr=subprocess.STDOUT, timeout=1800)
    return result.returncode


def themes(runtime, work, report):
    target = runtime / "editors" / "sdkjs" / "slide" / "themes"
    images = runtime / "editors" / "sdkjs" / "common" / "Images"
    if (target / "themes.js").is_file():
        report["themes"] = {"skipped": "themes.js is there already"}
        return True
    converter = runtime / "converter"
    for tool in ("allfontsgen.exe", "allthemesgen.exe", "x2t.exe"):
        if not (converter / tool).is_file():
            sys.exit(f"{converter / tool} is missing")
    source = work / "themes"
    shutil.copytree(target, source)
    (work / "Images").mkdir()
    fonts = run([str(converter / "allfontsgen.exe"), "--use-system=1", f"--input={runtime / 'fonts'}",
                 f"--allfonts={work / 'AllFonts.js'}", f"--selection={work / 'font_selection.bin'}"],
                converter, work / "allfontsgen.log")
    generated = run([str(converter / "allthemesgen.exe"), f"--converter-dir={converter}", f"--src={source}",
                     f"--allfonts={work / 'AllFonts.js'}", f"--output={work / 'Images'}"],
                    converter, work / "allthemesgen.log")
    if fonts or generated or not (source / "themes.js").is_file():
        report["themes"] = {"allfontsgen": fonts, "allthemesgen": generated, "ok": False}
        return False
    added = []
    for item in sorted(source.iterdir()):
        if item.name == "src":
            continue
        destination = target / item.name
        if destination.exists():
            sys.exit(f"{destination} exists; refusing to overwrite")
        if item.is_dir():
            shutil.copytree(item, destination)
            added += [f for f in destination.rglob("*") if f.is_file()]
        else:
            shutil.copy2(item, destination)
            added.append(destination)
    for item in sorted((work / "Images").iterdir()):
        destination = images / item.name
        if destination.exists():
            sys.exit(f"{destination} exists; refusing to overwrite")
        shutil.copy2(item, destination)
        added.append(destination)
    names = (target / "themes.js").read_text(encoding="utf-8")
    report["themes"] = {"ok": True, "themes_js": names.strip()[:4000], "files_added": len(added),
                        "added": {str(f.relative_to(runtime)).replace("\\", "/"): sha256(f) for f in added}}
    return True


def locales(runtime, report):
    changed = {}
    for zh in sorted((runtime / "editors" / "web-apps" / "apps").glob("*/main/locale/zh.json")):
        en = zh.with_name("en.json")
        if not en.is_file():
            continue
        chinese = json.loads(zh.read_bytes().decode("utf-8-sig"))
        english = json.loads(en.read_bytes().decode("utf-8-sig"))
        missing = [key for key in english if key not in chinese]
        if not missing:
            continue
        before = sha256(zh)
        for key in missing:
            chinese[key] = english[key]
        zh.write_text(json.dumps(chinese, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        changed[str(zh.relative_to(runtime)).replace("\\", "/")] = {
            "filled": len(missing), "sha256_before": before, "sha256_after": sha256(zh), "keys": missing}
    report["locales"] = changed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("runtime", type=Path)
    args = parser.parse_args()
    runtime = (args.runtime if args.runtime.is_absolute() else ROOT / args.runtime).resolve()
    if not (runtime / "OpenOffice.exe").is_file():
        sys.exit(f"{runtime} is no runtime")
    record_path = EVIDENCE / f"runtime-extras-{runtime.name}.json"
    if record_path.exists():
        sys.exit(f"{record_path} exists")
    work = SCRATCH / runtime.name
    if work.exists():
        sys.exit(f"{work} exists; pick another runtime or remove it")
    work.mkdir(parents=True)
    report = {"kind": "runtime-extras", "runtime": str(runtime),
              "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    ok = themes(runtime, work, report)
    locales(runtime, report)
    report["ok"] = ok
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with open(record_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"ok": ok, "themes": len(report.get("themes", {}).get("added", {})),
                      "locales": {k: v["filled"] for k, v in report["locales"].items()}, "record": str(record_path)},
                     ensure_ascii=False))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
