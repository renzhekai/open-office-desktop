#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Read-only ABI and source-drift check for the five SDKJS WebAssembly engines.

Compares the historical ONLYOFFICE core recipes and wrappers (restored from
075052dd into an isolated sparse checkout) with the pinned core tree and with
the WASM/JS assets deployed from the pinned SDKJS. Nothing from the inputs is
executed: JSON, C/C++ text and WASM binaries are only parsed.

Usage:
    python -I scripts/check_wasm_history_abi.py --output evidence/wasm-source/<new>.json

The output file must not exist yet; existing evidence is never overwritten.
"""

import argparse
import datetime
import hashlib
import json
import posixpath
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HISTORY_COMMIT = "075052dd3f41373affa533317d6734a0d34e0897"
DEFAULT_HISTORY = ROOT / "build" / "wasm-source" / "history-075052dd"
DEFAULT_CORE = ROOT / "build" / "wasm-source" / "core"
DEFAULT_SDKJS = ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "deploy" / "sdkjs"
# Native full-source tree whose 3dParty folders were already fetched/prepared for the MSVC build.
DEFAULT_PREPARED = ROOT / "build" / "full-source" / "DesktopEditors" / "core"

# name, historical recipe (core-relative), current loader JS and WASM (SDKJS deploy-relative)
MODULES = [
    ("fonts", "DesktopEditor/fontengine/js/libfont.json",
     "common/libfont/engine/fonts.js", "common/libfont/engine/fonts.wasm"),
    ("zlib", "OfficeUtils/js/zlib_image.json",
     "common/zlib/engine/zlib.js", "common/zlib/engine/zlib.wasm"),
    ("spell", "Common/3dParty/hunspell/hunspell.json",
     "common/spell/spell/spell.js", "common/spell/spell/spell.wasm"),
    ("hash", "DesktopEditor/xmlsec/src/wasm/hash/hash.json",
     "common/hash/hash/engine.js", "common/hash/hash/engine.wasm"),
    ("drawingfile", "DesktopEditor/graphics/pro/js/drawingfile.json",
     "pdf/src/engine/drawingfile.js", "pdf/src/engine/drawingfile.wasm"),
]

DRAWINGFILE_WRAPPER = "DesktopEditor/graphics/pro/js/wasm/src/drawingfile.cpp"
DRAWINGFILE_CLASS_HEADER = "DesktopEditor/doctrenderer/drawingfile.h"
MEMORY_STORAGE_HEADER = "DesktopEditor/graphics/pro/MemoryStorage.h"

VALTYPES = {0x7F: "i32", 0x7E: "i64", 0x7D: "f32", 0x7C: "f64",
            0x7B: "v128", 0x70: "funcref", 0x6F: "externref"}
EMSCRIPTEN_RUNTIME = {"_malloc", "_free"}
# Emscripten runtime internals referenced by generated glue, not ONLYOFFICE wrapper exports.
RUNTIME_INTERNAL_RE = re.compile(r"^(___|_setThrew$|_stack(Save|Restore|Alloc)$|_emscripten_)")
C_SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_tree(repo, rev):
    out = subprocess.run(["git", "-C", str(repo), "ls-tree", "-r", "-z", rev],
                         check=True, capture_output=True).stdout
    blobs = {}
    for entry in out.split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        _mode, kind, sha = meta.split(b" ")
        if kind == b"blob":
            blobs[path.decode("utf-8")] = sha.decode("ascii")
    return blobs


def git_head(repo):
    return subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()


# --- WebAssembly binary --------------------------------------------------------

def read_leb(data, pos):
    result = shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return result, pos


def read_name(data, pos):
    size, pos = read_leb(data, pos)
    return data[pos:pos + size].decode("utf-8"), pos + size


def skip_limits(data, pos):
    flags = data[pos]
    _, pos = read_leb(data, pos + 1)
    if flags & 1:
        _, pos = read_leb(data, pos)
    return pos


def wasm_exports(path):
    """Return ({function export: {"params": [...], "results": [...]}}, [global export names])."""
    data = path.read_bytes()
    if data[:4] != b"\0asm":
        raise ValueError(f"{path} is not a WebAssembly binary")
    pos = 8
    types, func_types, exports, globals_ = [], [], {}, []
    while pos < len(data):
        section = data[pos]
        size, pos = read_leb(data, pos + 1)
        end = pos + size
        if section == 1:
            count, p = read_leb(data, pos)
            for _ in range(count):
                if data[p] != 0x60:
                    raise ValueError(f"{path}: unsupported type form {data[p]:#x}")
                n, p = read_leb(data, p + 1)
                params = [VALTYPES.get(b, hex(b)) for b in data[p:p + n]]
                n, p = read_leb(data, p + n)
                results = [VALTYPES.get(b, hex(b)) for b in data[p:p + n]]
                types.append((params, results))
                p += n
        elif section == 2:
            count, p = read_leb(data, pos)
            for _ in range(count):
                _, p = read_name(data, p)
                _, p = read_name(data, p)
                kind = data[p]
                p += 1
                if kind == 0:
                    index, p = read_leb(data, p)
                    func_types.append(index)
                elif kind == 1:
                    p = skip_limits(data, p + 1)
                elif kind == 2:
                    p = skip_limits(data, p)
                elif kind == 3:
                    p += 2
                elif kind == 4:
                    _, p = read_leb(data, p + 1)
                else:
                    raise ValueError(f"{path}: unsupported import kind {kind}")
        elif section == 3:
            count, p = read_leb(data, pos)
            for _ in range(count):
                index, p = read_leb(data, p)
                func_types.append(index)
        elif section == 7:
            count, p = read_leb(data, pos)
            for _ in range(count):
                name, p = read_name(data, p)
                kind = data[p]
                index, p = read_leb(data, p + 1)
                if kind == 0:
                    params, results = types[func_types[index]]
                    exports[name] = {"params": params, "results": results}
                elif kind == 3:
                    globals_.append(name)
        pos = end
    return exports, globals_


# --- JS glue -----------------------------------------------------------------------

# Minified export names may contain "$" (for example wasmExports["$a"]).
GLUE_RE = re.compile(r'Module\["(_\w+)"\]\s*=\s*(?:wasmExports|Module\["asm"\]|asm)\["([\w$]+)"\]')
LOADER_USE_RE = re.compile(r'Module\["(_\w+)"\]')


def loader_exports(js_path, functions, globals_):
    text = js_path.read_text(encoding="utf-8", errors="replace")
    mapping = dict(GLUE_RE.findall(text))
    used = set(LOADER_USE_RE.findall(text))
    result = {}
    for name in sorted(used | set(mapping)):
        short = mapping.get(name) or (name[1:] if name[1:] in functions or name[1:] in globals_ else None)
        result[name] = {"wasm_export": short, "signature": functions.get(short),
                        "is_global": short in globals_}
    return result


# --- C/C++ text --------------------------------------------------------------------

def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def split_args(text):
    text = text.strip()
    if not text or text == "void":
        return []
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    parts.append(current.strip())
    return parts


def c_type_to_wasm(param):
    declaration = param.split("=")[0]
    if "*" in declaration or "&" in declaration:
        return "i32"
    if re.search(r"\b(long\s+long|int64_t|uint64_t|__int64|LONG64|ULONG64)\b", declaration):
        return "i64"
    if re.search(r"\bdouble\b", declaration):
        return "f64"
    if re.search(r"\bfloat\b", declaration):
        return "f32"
    return "i32"


def find_c_definition(texts, name):
    pattern = re.compile(
        r'^[ \t]*(?:WASM_EXPORT\s+|EMSCRIPTEN_KEEPALIVE\s+|static\s+|inline\s+)*'
        r'((?:[\w:<>]+[\s\*&]+)+?)' + re.escape(name) + r'\s*\(([^()]*)\)\s*\{', re.M)
    for rel, text in texts.items():
        match = pattern.search(text)
        if match:
            params = split_args(match.group(2))
            return {"file": rel, "return": " ".join(match.group(1).split()),
                    "params": params, "wasm_params": [c_type_to_wasm(p) for p in params]}
    return None


def resolve(base_dir, relative):
    return posixpath.normpath(posixpath.join(base_dir, relative))


def recipe_sources(recipe_path, recipe):
    base = posixpath.dirname(recipe_path)
    files = []
    for group in recipe.get("compile_files_array", []):
        folder = resolve(base, group.get("folder", "."))
        files.extend(resolve(folder, item) for item in group.get("files", []))
    files.extend(resolve(base, item) for item in recipe.get("sources", []))
    return files


def classify_sources(files, current, history, prepared_root):
    """Classify recipe inputs by git blob in the historical and pinned trees.

    Paths in neither tree are either produced by the recipe's run_before step
    (patched FreeType/OpenSSL copies), fetched 3dParty sources, or stale recipe
    entries; the prepared native tree tells the fetched ones apart.
    """
    current_ci = {k.lower(): k for k in current}
    history_ci = {k.lower(): k for k in history}
    summary = {"total": len(files), "same_blob": 0, "changed_since_history": [],
               "removed_after_history": [], "case_only_mismatch": [],
               "fetched_3dparty_present_in_prepared_tree": [], "absent_everywhere": []}
    for path in files:
        old = history.get(path) or history.get(history_ci.get(path.lower(), ""))
        new = current.get(path) or current.get(current_ci.get(path.lower(), ""))
        if (old or new) and path not in history and path not in current:
            summary["case_only_mismatch"].append(
                {"recipe": path, "tree": history_ci.get(path.lower()) or current_ci.get(path.lower())})
        if old and new:
            if old == new:
                summary["same_blob"] += 1
            else:
                summary["changed_since_history"].append(path)
        elif old:
            summary["removed_after_history"].append(path)
        elif new:
            summary.setdefault("added_after_history", []).append(path)
        elif (prepared_root / path).is_file():
            summary["fetched_3dparty_present_in_prepared_tree"].append(path)
        else:
            summary["absent_everywhere"].append(path)
    return summary


def compare_exports(recipe, loader, wrapper_texts):
    recipe_names = set(recipe.get("exported_functions", []))
    loader_names = set(loader)
    rows = []
    for name in sorted(recipe_names | loader_names):
        info = loader.get(name, {})
        signature = info.get("signature")
        definition = None if name in EMSCRIPTEN_RUNTIME else find_c_definition(wrapper_texts, name[1:])
        row = {"name": name, "in_historical_recipe": name in recipe_names,
               "used_by_current_loader": name in loader_names,
               "current_wasm_is_global": info.get("is_global", False),
               "current_wasm_signature": signature, "historical_definition": definition}
        if signature and definition:
            row["param_types_match"] = signature["params"] == definition["wasm_params"]
        rows.append(row)
    return rows


# --- drawingfile class API ---------------------------------------------------------

def class_methods(header_text):
    methods = {}
    pattern = re.compile(r'^\t(?:virtual\s+|static\s+|inline\s+)*[\w:<>\*&\s]*?\b(\w+)\s*\(([^()]*)\)\s*(?:const\s*)?(?:\{|$)', re.M)
    for name, params in pattern.findall(header_text):
        if name in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        parsed = split_args(params)
        required = sum(1 for p in parsed if "=" not in p)
        methods.setdefault(name, []).append({"params": parsed, "required": required, "max": len(parsed)})
    return methods


def drawingfile_api_check(history_root, core_root, loader, recipe):
    wrapper = strip_comments((history_root / DRAWINGFILE_WRAPPER).read_text(encoding="utf-8", errors="replace"))
    header = strip_comments((core_root / DRAWINGFILE_CLASS_HEADER).read_text(encoding="utf-8", errors="replace"))
    methods = class_methods(header)

    calls = []
    for match in re.finditer(r'pFile->(\w+)\s*\(', wrapper):
        start = match.end()
        depth, pos = 1, start
        while depth and pos < len(wrapper):
            depth += {"(": 1, ")": -1}.get(wrapper[pos], 0)
            pos += 1
        argc = len(split_args(wrapper[start:pos - 1]))
        candidates = methods.get(match.group(1), [])
        calls.append({"method": match.group(1), "historical_argc": argc,
                      "current_overloads": [{"required": c["required"], "max": c["max"]} for c in candidates],
                      "compatible": any(c["required"] <= argc <= c["max"] for c in candidates)})

    recipe_names = set(recipe.get("exported_functions", []))
    new_exports = []
    storage_header = strip_comments((core_root / MEMORY_STORAGE_HEADER).read_text(encoding="utf-8", errors="replace"))
    storage_has_clear = bool(re.search(r"virtual\s+void\s+Clear\s*\(\s*\)\s*=\s*0", storage_header))
    for name in sorted(set(loader) - recipe_names - EMSCRIPTEN_RUNTIME):
        if loader[name].get("is_global") or RUNTIME_INTERNAL_RE.match(name):
            continue
        signature = loader[name].get("signature")
        candidates = methods.get(name[1:], [])
        wasm_argc = len(signature["params"]) if signature else None
        item = {
            "name": name, "current_wasm_signature": signature,
            "core_method_overloads": [{"params": c["params"], "required": c["required"], "max": c["max"]}
                                      for c in candidates],
            "fits_instance_wrapper": any(c["required"] <= (wasm_argc or 0) - 1 <= c["max"] for c in candidates),
            "fits_global_wrapper": any(c["required"] <= (wasm_argc or 0) <= c["max"] for c in candidates),
        }
        clear = re.fullmatch(r"_Clear(\w+)Storage", name)
        if clear:
            getter = f"Get{clear.group(1)}Storage"
            item["storage_clear_backing"] = {
                "static_getter": getter, "getter_in_class": getter in methods,
                "storage_interface": MEMORY_STORAGE_HEADER, "interface_has_pure_virtual_clear": storage_has_clear,
                "zero_arg_wasm_matches": wasm_argc == 0,
            }
        new_exports.append(item)
    unique_calls = {}
    for call in calls:
        unique_calls.setdefault((call["method"], call["historical_argc"]), call)
    return {"wrapper": DRAWINGFILE_WRAPPER, "class_header": DRAWINGFILE_CLASS_HEADER,
            "wrapper_calls": list(unique_calls.values()),
            "incompatible_calls": [c for c in unique_calls.values() if not c["compatible"]],
            "exports_missing_from_historical_wrapper": new_exports}


# --- main --------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--history", type=Path, default=DEFAULT_HISTORY)
    parser.add_argument("--core", type=Path, default=DEFAULT_CORE)
    parser.add_argument("--sdkjs", type=Path, default=DEFAULT_SDKJS)
    parser.add_argument("--prepared", type=Path, default=DEFAULT_PREPARED)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    output = args.output if args.output.is_absolute() else ROOT / args.output
    if output.exists():
        sys.exit(f"refusing to overwrite existing evidence: {output}")

    history_head = git_head(args.history)
    if history_head != HISTORY_COMMIT:
        sys.exit(f"history checkout is at {history_head}, expected {HISTORY_COMMIT}")
    current = git_tree(args.core, "HEAD")
    history = git_tree(args.history, "HEAD")

    modules = []
    for name, recipe_rel, loader_rel, wasm_rel in MODULES:
        recipe_path = args.history / recipe_rel
        recipe = json.loads(recipe_path.read_text(encoding="utf-8"))
        wasm_path, js_path = args.sdkjs / wasm_rel, args.sdkjs / loader_rel
        functions, globals_ = wasm_exports(wasm_path)
        loader = loader_exports(js_path, functions, globals_)
        sources = recipe_sources(recipe_rel, recipe)

        wrapper_texts = {}
        recipe_dir = posixpath.dirname(recipe_rel)
        for rel in sorted(history):
            local = args.history / rel
            in_scope = rel in sources or rel.startswith(recipe_dir + "/")
            if in_scope and rel.endswith(C_SOURCE_SUFFIXES) and local.is_file():
                wrapper_texts[rel] = strip_comments(local.read_text(encoding="utf-8", errors="replace"))

        exports = compare_exports(recipe, loader, wrapper_texts)
        entry = {
            "module": name,
            "recipe": {"path": recipe_rel, "sha256": sha256(recipe_path),
                       "run_before": recipe.get("run_before"), "run_after": recipe.get("run_after")},
            "current_wasm": {"path": wasm_rel, "bytes": wasm_path.stat().st_size, "sha256": sha256(wasm_path),
                             "function_exports": len(functions), "global_exports": globals_},
            "current_loader": {"path": loader_rel, "sha256": sha256(js_path)},
            "sources": classify_sources(sources, current, history, args.prepared),
            "exports": exports,
            "export_summary": {
                "recipe_only": [r["name"] for r in exports if r["in_historical_recipe"] and not r["used_by_current_loader"]],
                "loader_only": [r["name"] for r in exports if r["used_by_current_loader"] and not r["in_historical_recipe"]
                                and not RUNTIME_INTERNAL_RE.match(r["name"]) and not r["current_wasm_is_global"]],
                "loader_only_runtime_internal": [r["name"] for r in exports if r["used_by_current_loader"]
                                                 and not r["in_historical_recipe"]
                                                 and (RUNTIME_INTERNAL_RE.match(r["name"]) or r["current_wasm_is_global"])],
                "missing_in_current_wasm": [r["name"] for r in exports
                                            if r["used_by_current_loader"] and not r["current_wasm_signature"]
                                            and not r["current_wasm_is_global"]
                                            and not RUNTIME_INTERNAL_RE.match(r["name"])],
                "historical_definition_not_found": [r["name"] for r in exports
                                                    if r["name"] not in EMSCRIPTEN_RUNTIME
                                                    and r["in_historical_recipe"] and not r["historical_definition"]],
                "param_type_mismatch": [r["name"] for r in exports if r.get("param_types_match") is False],
            },
        }
        if name == "drawingfile":
            entry["drawingfile_api"] = drawingfile_api_check(args.history, args.core, loader, recipe)
        modules.append(entry)

    report = {
        "schema_version": 1,
        "checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "read-only parse of historical recipes/wrappers, pinned core tree and deployed SDKJS WASM/JS; "
                 "no compiler, emsdk or input code executed",
        "history_commit": HISTORY_COMMIT,
        "core_commit": git_head(args.core),
        "sdkjs_deploy": str(args.sdkjs),
        "modules": modules,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with open(output, "x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    for entry in modules:
        s, e = entry["sources"], entry["export_summary"]
        print(f"{entry['module']}: sources {s['total']} same={s['same_blob']} changed={len(s['changed_since_history'])} "
              f"removed={len(s['removed_after_history'])} case={len(s['case_only_mismatch'])} "
              f"fetched={len(s['fetched_3dparty_present_in_prepared_tree'])} absent={len(s['absent_everywhere'])}; "
              f"loader_only={e['loader_only']} missing_in_wasm={e['missing_in_current_wasm']} "
              f"no_definition={e['historical_definition_not_found']} type_mismatch={e['param_type_mismatch']}")
        if "drawingfile_api" in entry:
            api = entry["drawingfile_api"]
            print(f"  drawingfile incompatible calls: {[(c['method'], c['historical_argc'], c['current_overloads']) for c in api['incompatible_calls']]}")
            for item in api["exports_missing_from_historical_wrapper"]:
                print(f"  new export {item['name']}: wasm={item['current_wasm_signature']} "
                      f"instance_fit={item['fits_instance_wrapper']} global_fit={item['fits_global_wrapper']} "
                      f"clear={item.get('storage_clear_backing')}")
    print(f"written {output}")


if __name__ == "__main__":
    main()
