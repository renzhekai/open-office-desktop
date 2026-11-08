"""Acquire and prepare graphics-only dependencies in the isolated source tree.

All Git settings are per process. No installer, global configuration, upstream
checkout, or prebuilt core is touched. Build orchestration is separate.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "build/full-source"
THIRD = AREA / "DesktopEditors/core/Common/3dParty"
EVIDENCE = AREA / "graphics-dependency-evidence"
MANIFEST = EVIDENCE / "sources.json"
SOURCES = {
    "katana": ("html/katana-parser", "https://github.com/jasenhuang/katana-parser.git", "be6df458d4540eee375c513958dcb862a391cdd1", None),
    "harfbuzz": ("harfbuzz/harfbuzz", "https://github.com/harfbuzz/harfbuzz.git", "894a1f72ee93a1fd8dc1d9218cb3fd8f048be29a", None),
    "brotli": ("brotli/brotli", "https://github.com/google/brotli.git", "a47d7475063eb223c87632eed806c0070e70da29", None),
    "hyphen": ("hyphen/hyphen", "https://github.com/hunspell/hyphen.git", None, "HEAD"),
    "x265": ("heif/x265", "https://github.com/Multicorewareinc/x265.git", None, "refs/heads/Release_4.1"),
    "libde265": ("heif/libde265", "https://github.com/strukturag/libde265.git", None, "refs/tags/v1.0.16"),
    "libheif": ("heif/libheif", "https://github.com/strukturag/libheif.git", None, "refs/tags/v1.18.2"),
}
ENV = os.environ.copy()
ENV.update(GIT_TERMINAL_PROMPT="0", GIT_NO_LAZY_FETCH="1", GIT_OPTIONAL_LOCKS="0")


def save(data):
    MANIFEST.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def git(log, *arguments, cwd=None):
    command = ["git", "-c", "core.hooksPath=" + str(AREA / "empty-hooks"), *arguments]
    log.write(json.dumps({"argv": command, "cwd": str(cwd) if cwd else None}) + "\n")
    log.flush()
    result = subprocess.run(command, cwd=cwd, env=ENV, capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    log.write(result.stdout + result.stderr + "\nexit_code=" + str(result.returncode) + "\n")
    log.flush()
    if result.returncode:
        raise RuntimeError(result.stderr[-6000:])
    return result.stdout.strip()


def acquire(data, names):
    for name in names:
        relative, repository, pinned, ref = SOURCES[name]
        destination = THIRD / relative
        if not destination.resolve().is_relative_to(THIRD.resolve()):
            raise ValueError("Source directory escapes isolated tree")
        record = data.setdefault(name, {"repository": repository, "relative_path": relative,
                                       "requested_ref": ref, "commit": pinned})
        if record["repository"] != repository or record["relative_path"] != relative:
            raise ValueError("Recorded source disagrees with this script")
        with (EVIDENCE / (name + "-git.log")).open("a", encoding="utf-8") as log:
            if not record.get("commit"):
                refs = [ref] if ref == "HEAD" else [ref, ref + "^{}"]
                output = git(log, "ls-remote", "--exit-code", repository, *refs)
                values = dict(line.split()[::-1] for line in output.splitlines())
                record["commit"] = values.get(ref + "^{}", values.get(ref))
                if not record["commit"] or len(record["commit"]) != 40:
                    raise ValueError("No exact remote commit resolved")
                record["resolved_at_utc"] = datetime.now(timezone.utc).isoformat()
                record["ref_resolution"] = output
                save(data)
            if not destination.exists():
                destination.mkdir(parents=True)
                record["created_by_script"] = True
                save(data)
            elif not (destination / ".git").is_dir() and not record.get("created_by_script"):
                raise FileExistsError("Unowned existing source directory: " + str(destination))
            if not (destination / ".git").is_dir():
                if any(destination.iterdir()):
                    raise FileExistsError("Inspect interrupted nonempty source directory")
                git(log, "init", "-q", cwd=destination)
                git(log, "remote", "add", "origin", repository, cwd=destination)
            if git(log, "remote", "get-url", "origin", cwd=destination) != repository:
                raise ValueError("Existing repository has an unexpected origin")
            status = subprocess.run(["git", "-C", str(destination), "rev-parse", "--verify", "HEAD"],
                                    capture_output=True, text=True, env=ENV)
            if status.returncode == 0:
                if status.stdout.strip() != record["commit"]:
                    raise ValueError("Existing checkout differs from recorded commit")
            else:
                git(log, "fetch", "--depth", "1", "origin", record["commit"], cwd=destination)
                git(log, "checkout", "--detach", "FETCH_HEAD", cwd=destination)
            record["checked_out_commit"] = git(log, "rev-parse", "HEAD", cwd=destination)
            record["status"] = "checked_out"
            save(data)
            print(name + " checked_out " + record["commit"], flush=True)


def replace_once(path, old, new, patches, expected=1):
    source = path.read_text(encoding="utf-8")
    if source.count(new) == expected:
        return
    if source.count(old) != expected or new in source:
        raise ValueError("Unexpected patch anchor count in " + str(path))
    original = hashlib.sha256(source.encode()).hexdigest()
    source = source.replace(old, new)
    path.write_text(source, encoding="utf-8", newline="\n")
    patches.append({"path": str(path), "before_sha256": original,
                    "after_sha256": hashlib.sha256(source.encode()).hexdigest()})
    (EVIDENCE / "source-preparation.json").write_text(json.dumps(patches, indent=2) + "\n", encoding="utf-8")


def prepare(data):
    receipt = EVIDENCE / "source-preparation.json"
    patches = json.loads(receipt.read_text(encoding="utf-8")) if receipt.exists() else []
    katana = THIRD / SOURCES["katana"][0]
    replace_once(katana / "src/tokenizer.c", "inline bool katana_is_html_space(char c)\n",
                 "static inline bool katana_is_html_space(char c)\n", patches)
    replace_once(katana / "src/parser.c", "katanaget_text(parser->scanner)",
                 '/*katanaget_text(parser->scanner)*/"error"', patches, expected=2)
    replace_once(katana / "src/parser.c", "#define KATANA_PARSER_STRING(literal) (KatanaParserString){",
                 "#define KATANA_PARSER_STRING(literal) {", patches)
    replace_once(katana / "src/foundation.c", "size_t len = strlen(str);",
                 "if (NULL == str)\n        return;\n    size_t len = strlen(str);", patches, expected=2)
    hb_parent = THIRD / "harfbuzz"
    patch = (hb_parent / "patch/hb-ft.cc.patch").read_text(encoding="utf-8")
    begin, middle, end = patch.index("<<<<<<<"), patch.index("======="), patch.index(">>>>>>>")
    old = patch[begin + 7:middle]
    new = "\n#if 0" + old + "#else" + patch[middle + 7:end] + "#endif\n"
    replace_once(hb_parent / "harfbuzz/src/hb-ft.cc", old, new, patches)
    hb = hb_parent / "harfbuzz/src"
    files = sorted(p.name for p in hb.iterdir() if p.is_file())
    files += sorted("graph/" + p.name for p in (hb / "graph").iterdir() if p.is_file())
    headers = [p for p in files if Path(p).suffix in (".h", ".hh")]
    sources = [p for p in files if Path(p).suffix in (".c", ".cc")
               and Path(p).name not in ("main.cc", "failing-alloc.c")
               and not Path(p).name.startswith(("test", "harfbuzz"))]
    content = "SRC_DIR = $$PWD/harfbuzz/src\nDEFINES += HAVE_FREETYPE HB_NO_VISIBILITY\nINCLUDEPATH += $$SRC_DIR\n\n"
    for label, entries in (("HEADERS", headers), ("SOURCES", sources)):
        content += label + " += \\\n" + " \\\n".join("  $$SRC_DIR/" + p for p in entries) + "\n\n"
    generated = hb_parent / "harfbuzz.pri"
    generated.write_text(content, encoding="utf-8", newline="\n")
    patches.append({"path": str(generated), "generated": True, "headers": len(headers),
                    "sources": len(sources), "sha256": hashlib.sha256(content.encode()).hexdigest()})
    replace_once(THIRD / "heif/x265/source/cmake/Version.cmake",
                 "elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../x265Version.txt)",
                 "endif()\n    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../x265Version.txt)", patches)
    heif_cmake = THIRD / "heif/libheif/CMakeLists.txt"
    if "add_subdirectory(heifio)" in heif_cmake.read_text(encoding="utf-8"):
        replace_once(heif_cmake, "add_subdirectory(heifio)", "# add_subdirectory(heifio)", patches)
    else:
        patches.append({"path": str(heif_cmake), "patch": "disable heifio",
                        "status": "not_applicable_pinned_v1.18.2_has_no_heifio_subdirectory"})
    replace_once(THIRD / "heif/libheif/CMakeLists.txt", "if (DOXYGEN_FOUND)", "if (FALSE)", patches, expected=2)
    (EVIDENCE / "source-preparation.json").write_text(json.dumps(patches, indent=2) + "\n", encoding="utf-8")
    print("Prepared pinned sources and harfbuzz.pri", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("acquire", "prepare"))
    parser.add_argument("--source", choices=list(SOURCES))
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    data = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.exists() else {}
    if args.action == "acquire":
        acquire(data, [args.source] if args.source else list(SOURCES))
    else:
        prepare(data)


if __name__ == "__main__":
    main()
