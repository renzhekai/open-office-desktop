"""Prepare ONLYOFFICE's pinned Gumbo, Hunspell, iWork and Markdown dependencies.

Only isolated dependency checkouts and dedicated evidence are written. The
upstream fetch/before scripts are parsed, never executed. No global Git settings,
installer, top-level make, cleanup routine, or other dependency subtree is used.
"""
import argparse
import ast
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "build/full-source"
THIRD = AREA / "DesktopEditors/core/Common/3dParty"
EVIDENCE = AREA / "remaining-dependency-evidence"
MANIFEST = EVIDENCE / "sources.json"
ALIAS = Path(r"C:\Users\18268\AppData\Local\Temp\oo10-full-src")
SOURCES = {
    "gumbo": ("html/gumbo-parser", "https://github.com/google/gumbo-parser.git", "aa91b27b02c0c80c482e24348a457ed7c3c088e0"),
    "hunspell": ("hunspell/hunspell", "https://github.com/hunspell/hunspell.git", "8a2fdfe5a6bb1cbafc04b0c8486abcefd17ad903"),
    "glm": ("apple/glm", "https://github.com/g-truc/glm.git", "33b4a621a697a305bc3a7610d290677b96beb181"),
    "mdds": ("apple/mdds", "https://github.com/kohei-us/mdds.git", "0783158939c6ce4b0b1b89e345ab983ccb0f0ad0"),
    "librevenge": ("apple/librevenge", "https://github.com/Distrotech/librevenge.git", "becd044b519ab83893ad6398e3cbb499a7f0aaf4"),
    "libodfgen": ("apple/libodfgen", "https://github.com/Distrotech/libodfgen.git", "8ef8c171ebe3c5daebdce80ee422cf7bb96aa3bc"),
    "libetonyek": ("apple/libetonyek", "https://github.com/LibreOffice/libetonyek.git", "cb396b4a9453a457469b62a740d8fb933c9442c3"),
    "md4c": ("md/md4c", "https://github.com/mity/md4c.git", "481fbfbdf72daab2912380d62bb5f2187d438408"),
}
ENV = os.environ.copy()
ENV.update(GIT_TERMINAL_PROMPT="0", GIT_NO_LAZY_FETCH="1", GIT_OPTIONAL_LOCKS="0")


def stamp():
    return datetime.now(timezone.utc).isoformat()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def run(label, argv, cwd=None, env=None, allow_failure=False, timeout=600, redact_output=False):
    with (EVIDENCE / (label + ".log")).open("a", encoding="utf-8") as log:
        log.write(json.dumps({"utc": stamp(), "argv": [str(a) for a in argv], "cwd": str(cwd)}) + "\n")
        log.flush()
        result = subprocess.run([str(a) for a in argv], cwd=cwd, env=env or ENV,
                                capture_output=True, timeout=timeout)
        if redact_output:
            log.write("Environment values captured for this child build only; output omitted from log.\n")
        else:
            log.write(result.stdout.decode("utf-8", "replace") + result.stderr.decode("utf-8", "replace"))
        log.write("\nEXIT_CODE=" + str(result.returncode) + "\n")
    if result.returncode and not allow_failure:
        raise RuntimeError(label + " failed: " + (result.stdout + result.stderr).decode("utf-8", "replace")[-6000:])
    return result


def git(*args, cwd=None, allow_failure=False):
    return run("git", ["git", "-c", "core.hooksPath=" + str(AREA / "empty-hooks"),
                       "-c", "core.longpaths=true", "-c", "core.autocrlf=false",
                       "-c", "http.version=HTTP/1.1", *args], cwd, allow_failure=allow_failure)


def git_text(*args, cwd=None):
    return git(*args, cwd=cwd).stdout.decode("utf-8").strip()


def check_recipes():
    html = (THIRD / "html/fetch.py").read_text(encoding="utf-8")
    apple = (THIRD / "apple/fetch.py").read_text(encoding="utf-8")
    hunspell = (THIRD / "hunspell/before.py").read_text(encoding="utf-8")
    markdown = (THIRD / "md/fetch.py").read_text(encoding="utf-8")
    for name, (_, url, commit) in SOURCES.items():
        recipe = hunspell if name == "hunspell" else html if name == "gumbo" else markdown if name == "md4c" else apple
        if url not in recipe or (name != "hunspell" and commit not in recipe):
            raise ValueError("Pinned source no longer agrees with upstream recipe: " + name)
    if (THIRD / "hunspell/HEAD").read_text().strip() != SOURCES["hunspell"][2]:
        raise ValueError("Hunspell HEAD differs from audited pin")


def acquire(names):
    check_recipes()
    data = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.exists() else {}
    for name in names:
        relative, url, commit = SOURCES[name]
        destination = THIRD / relative
        if not destination.resolve().is_relative_to(THIRD.resolve()):
            raise ValueError("Dependency directory escapes isolated tree")
        record = data.setdefault(name, {"repository": url, "commit": commit, "relative_path": relative})
        if (record["repository"], record["commit"], record["relative_path"]) != (url, commit, relative):
            raise ValueError("Existing source manifest disagrees with fixed source")
        save(MANIFEST, data)
        if not (destination / ".git").is_dir():
            if destination.exists() and any(destination.iterdir()):
                raise FileExistsError("Refusing unowned existing directory: " + str(destination))
            destination.mkdir(parents=True, exist_ok=True)
            git("init", "--initial-branch=source-build", str(destination))
            git("remote", "add", "origin", url, cwd=destination)
        if git_text("remote", "get-url", "origin", cwd=destination) != url:
            raise ValueError("Existing source origin differs: " + name)
        head = git("rev-parse", "--verify", "HEAD", cwd=destination, allow_failure=True)
        if head.returncode:
            print("Fetching " + name + " " + commit, flush=True)
            git("fetch", "--depth=1", "origin", commit, cwd=destination)
            git("checkout", "--detach", commit, cwd=destination)
        elif head.stdout.decode().strip() != commit:
            raise ValueError("Refusing to replace a different checkout: " + name)
        record.update(checked_out_commit=git_text("rev-parse", "HEAD", cwd=destination),
                      checked_at_utc=stamp(), status="checked_out",
                      source_role="upstream_recipe_mirror" if name in ("librevenge", "libodfgen") else "upstream_repository")
        save(MANIFEST, data)
        print(name + " verified " + commit, flush=True)


def literal(node, variables):
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if isinstance(node, ast.Name):
        return variables[node.id]
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return literal(node.left, variables) + literal(node.right, variables)
    raise ValueError("Not a static string expression")


def recipe_replacements(path):
    """Extract only literal base.replaceInFile calls; no upstream code executes."""
    variables, replacements = {}, []

    def visit(body):
        for statement in body:
            if isinstance(statement, ast.Assign) and len(statement.targets) == 1 and isinstance(statement.targets[0], ast.Name):
                try:
                    variables[statement.targets[0].id] = literal(statement.value, variables)
                except (ValueError, KeyError):
                    pass
            elif isinstance(statement, ast.AugAssign) and isinstance(statement.target, ast.Name) and isinstance(statement.op, ast.Add):
                try:
                    variables[statement.target.id] += literal(statement.value, variables)
                except (ValueError, KeyError):
                    pass
            elif isinstance(statement, (ast.If, ast.FunctionDef)):
                visit(statement.body)
                if isinstance(statement, ast.If):
                    visit(statement.orelse)
            elif isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call):
                call = statement.value
                if isinstance(call.func, ast.Attribute) and isinstance(call.func.value, ast.Name) and call.func.value.id == "base" and call.func.attr == "replaceInFile":
                    # Gumbo's one path uses base_directory = os.getcwd().
                    variables["base_directory"] = "."
                    replacements.append(tuple(literal(arg, variables) for arg in call.args))

    visit(ast.parse(path.read_text(encoding="utf-8")).body)
    return replacements


def repository_for(path):
    for name, (relative, _, _) in SOURCES.items():
        repository = THIRD / relative
        if path.resolve().is_relative_to(repository.resolve()):
            return name, repository, path.relative_to(repository).as_posix()
    raise ValueError("Patch target outside owned dependency sources: " + str(path))


def prepare():
    check_recipes()
    for name, (relative, _, commit) in SOURCES.items():
        if git_text("rev-parse", "HEAD", cwd=THIRD / relative) != commit:
            raise ValueError("Dependency revision differs: " + name)
    originals, prepared, patches = {}, {}, []
    recipes = [(THIRD / "html/fetch.py", THIRD / "html"),
               (THIRD / "hunspell/before.py", THIRD / "hunspell/hunspell"),
               (THIRD / "apple/fetch.py", THIRD / "apple")]
    for recipe, base in recipes:
        for relative, old, new in recipe_replacements(recipe):
            if recipe.parent.name == "html" and "gumbo-parser/" not in relative:
                continue
            path = base / relative.removeprefix("./")
            name, repository, repo_relative = repository_for(path)
            if path not in originals:
                originals[path] = git("show", "HEAD:" + repo_relative, cwd=repository).stdout.decode("utf-8").replace("\r\n", "\n")
                prepared[path] = originals[path]
            before = prepared[path]
            count = before.count(old)
            if not count:
                raise ValueError("Missing upstream patch anchor: " + str(path) + " " + old[:80])
            prepared[path] = before.replace(old, new)
            patches.append({"source": name, "path": str(path), "recipe": str(recipe),
                            "recipe_sha256": digest(recipe.read_bytes()), "occurrences": count,
                            "before_sha256": digest(before.encode()), "after_sha256": digest(prepared[path].encode())})
    for path, value in prepared.items():
        current = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        if current not in (originals[path], value):
            raise ValueError("Unrecognized local source modifications: " + str(path))
    for path, value in prepared.items():
        path.write_text(value, encoding="utf-8", newline="\n")
    tokens = []
    for source in sorted((THIRD / "apple/headers").glob("*.inc")):
        destination = THIRD / "apple/libetonyek/src/lib" / source.name
        if destination.exists() and destination.read_bytes() != source.read_bytes():
            raise ValueError("Refusing unrecognized generated token file: " + str(destination))
        destination.write_bytes(source.read_bytes())
        tokens.append({"source": str(source), "destination": str(destination), "sha256": digest(source.read_bytes()),
                       "provenance": "ONLYOFFICE tracked gperf output; upstream iwork.make(False) copies this file"})
    if len(tokens) != 5:
        raise ValueError("Expected five tracked token headers")
    result = {"prepared_at_utc": stamp(), "patches": patches, "token_headers": tokens,
              "gperf_executed": False, "unrelated_dependencies_modified": False}
    save(EVIDENCE / "preparation.json", result)
    print("Prepared " + str(len(patches)) + " upstream replacements and 5 token headers", flush=True)


def msvc_environment():
    vcvars = r'D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
    # A local batch file avoids subprocess.list2cmdline quoting cmd's /c string.
    wrapper = EVIDENCE / "capture-build-environment.cmd"
    wrapper.write_text('@echo off\ncall "' + vcvars + '" x64 -vcvars_ver=14.44.35207 >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="ascii", newline="\r\n")
    short_wrapper = ALIAS / "remaining-dependency-evidence" / wrapper.name
    result = run("vcvars", ["cmd.exe", "/d", "/c", short_wrapper], redact_output=True)
    environment = {}
    for line in result.stdout.decode("utf-8", "replace").splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            environment[key.upper()] = value
    if environment.get("VCTOOLSVERSION", "").rstrip("\\/") != "14.44.35207":
        raise ValueError("Unexpected compiler selected")
    for key in ("DESTDIR_BUILD_OVERRIDE", "DEST_MAKEFILE_NAME", "OO_BRANDING", "WINDOWS_VERSION_XP", "ADDITIONAL_INCLUDE_PATH", "QMAKEFEATURES", "QMAKEPATH", "CL", "_CL_", "LINK", "_LINK_", "MAKEFLAGS"):
        environment.pop(key, None)
    environment.update(PRODUCT_VERSION="10.0.0", BUILD_NUMBER="0")
    return environment


def build_hunspell():
    if ALIAS.resolve() != AREA.resolve():
        raise ValueError("Existing short-path alias does not identify isolated workspace")
    if not (EVIDENCE / "preparation.json").exists():
        raise ValueError("Prepare pinned sources before building")
    environment = msvc_environment()
    build = ALIAS / "remaining-dependency-evidence/hunspell-build"
    build.mkdir(parents=True, exist_ok=True)
    qmake = Path(r"D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe")
    project = ALIAS / "DesktopEditors/core/Common/3dParty/hunspell/qt/hunspell.pro"
    argv = [qmake, "-nocache", "-spec", "win32-msvc", "-o", "Makefile", project,
            "QMAKE_TARGET.arch=x86_64", "CONFIG-=debug", "CONFIG-=debug_and_release",
            "CONFIG-=debug_and_release_target", "CONFIG-=force_debug_info", "CONFIG-=separate_debug_info",
            "CONFIG-=core_debug", "CONFIG+=release", "CONFIG+=vs2022", "CONFIG+=desktop",
            "CONFIG+=disable_precompiled_header", "QMAKE_CXXFLAGS+=/utf-8", "-after",
            "CONFIG-=object_parallel_to_source", "OBJECTS_DIR=" + (build / "obj").as_posix()]
    run("hunspell-qmake", argv, build, environment)
    run("hunspell-build", [r"D:\Qt\Tools\QtCreator\bin\jom\jom.exe", "/J", "2", "/NOLOGO", "/F", "Makefile"], build, environment, timeout=1800)
    libraries = AREA / "DesktopEditors/core/build/lib/win_64"
    artifacts = []
    for name in ("hunspell.dll", "hunspell.lib"):
        path = libraries / name
        if not path.is_file() or not path.stat().st_size:
            raise FileNotFoundError("Expected source-built artifact missing: " + str(path))
        artifacts.append({"path": str(path), "bytes": path.stat().st_size, "sha256": digest(path.read_bytes())})
    result = {"completed_at_utc": stamp(), "configuration": "MSVC 14.44.35207 x64 Release /MD",
              "project": str(project), "artifacts": artifacts, "scope": "Hunspell upstream native dependency; not main core-module link"}
    save(EVIDENCE / "hunspell-build-result.json", result)
    print(json.dumps(result, indent=2), flush=True)


def smoke_hunspell():
    library = AREA / "DesktopEditors/core/build/lib/win_64/hunspell.dll"
    if not (EVIDENCE / "hunspell-build-result.json").exists():
        raise ValueError("Build Hunspell before smoke verification")
    dictionary = EVIDENCE / "smoke.dic"
    affix = EVIDENCE / "smoke.aff"
    dictionary.write_text("2\nhello\nworld\n", encoding="utf-8")
    affix.write_text("SET UTF-8\n", encoding="utf-8")
    dll = ctypes.CDLL(str(library))
    dll.Hunspell_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    dll.Hunspell_create.restype = ctypes.c_void_p
    dll.Hunspell_spell.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.Hunspell_spell.restype = ctypes.c_int
    dll.Hunspell_destroy.argtypes = [ctypes.c_void_p]
    dll.Hunspell_destroy.restype = None
    outcomes = []
    for _ in range(2):
        handle = dll.Hunspell_create(os.fsencode(affix), os.fsencode(dictionary))
        if not handle:
            raise RuntimeError("Hunspell dictionary initialization failed")
        try:
            outcomes.append({"hello": dll.Hunspell_spell(handle, b"hello"), "invalid": dll.Hunspell_spell(handle, b"zzinvalidword")})
        finally:
            dll.Hunspell_destroy(handle)
    if any(item != {"hello": 1, "invalid": 0} for item in outcomes):
        raise RuntimeError("Unexpected spelling results: " + repr(outcomes))
    save(EVIDENCE / "hunspell-smoke-result.json", {"passed": True, "library_sha256": digest(library.read_bytes()), "create_destroy_cycles": outcomes})
    print("Hunspell spelling and repeated create/destroy smoke passed", flush=True)


def verify():
    check_recipes()
    revisions = {}
    for name, (relative, _, commit) in SOURCES.items():
        actual = git_text("rev-parse", "HEAD", cwd=THIRD / relative)
        if actual != commit:
            raise ValueError("Source revision mismatch: " + name)
        revisions[name] = actual
    preparation = json.loads((EVIDENCE / "preparation.json").read_text(encoding="utf-8"))
    final_patches = {item["path"]: item for item in preparation["patches"]}
    for path, item in final_patches.items():
        if digest(Path(path).read_bytes()) != item["after_sha256"]:
            raise ValueError("Prepared source hash mismatch: " + path)
    for item in preparation["token_headers"]:
        if digest(Path(item["destination"]).read_bytes()) != item["sha256"]:
            raise ValueError("Generated token header differs")
    groups = {
        "gumbo": sorted((THIRD / "html/gumbo-parser/src").glob("*.c")),
        "librevenge": sorted((THIRD / "apple/librevenge/src/lib").rglob("*.cpp")),
        "libodfgen": sorted((THIRD / "apple/libodfgen/src").glob("*.cxx")),
        "libetonyek": sorted((THIRD / "apple/libetonyek/src/lib").rglob("*.cpp")),
        "md4c": [THIRD / "md/md4c/src" / name for name in ("md4c.c", "md4c-html.c", "entity.c")],
    }
    for name, paths in groups.items():
        if not paths or not all(path.is_file() and path.stat().st_size for path in paths):
            raise FileNotFoundError("Missing source input: " + name)
    result = {"verified_at_utc": stamp(), "revisions": revisions,
              "source_counts": {name: len(paths) for name, paths in groups.items()},
              "source_manifests": {name: [{"path": str(path.relative_to(THIRD)), "sha256": digest(path.read_bytes())} for path in paths] for name, paths in groups.items()},
              "upstream_patch_count": len(preparation["patches"]), "token_header_count": len(preparation["token_headers"]),
              "hunspell_build": json.loads((EVIDENCE / "hunspell-build-result.json").read_text(encoding="utf-8")),
              "hunspell_smoke": json.loads((EVIDENCE / "hunspell-smoke-result.json").read_text(encoding="utf-8")),
              "integration_pending": ["HtmlFile2/Fb2File compile Gumbo sources directly", "IWork compiles Apple dependency sources directly", "HtmlFile2 compiles md4c sources directly"],
              "original_upstream_modified": False, "system_tools_installed": False}
    save(EVIDENCE / "remaining-dependencies-result.json", result)
    print(json.dumps({"verified": True, "source_counts": result["source_counts"], "hunspell_smoke": result["hunspell_smoke"]["passed"]}), flush=True)


def record_modules():
    run_path = AREA / "module-runs/remaining-iwork-pass1/result.json"
    result = json.loads(run_path.read_text(encoding="utf-8"))
    if result.get("status") != "passed" or {entry["name"]: entry.get("exit_code") for entry in result["modules"]} != {"iwork": 0, "pluginsmanager": 0}:
        raise ValueError("Expected successful IWork and pluginsmanager build results")
    artifacts = []
    for relative in ("lib/win_64/IWorkFile.dll", "lib/win_64/IWorkFile.lib", "bin/win_64/pluginsmanager.exe"):
        path = AREA / "DesktopEditors/core/build" / relative
        if not path.is_file() or not path.stat().st_size:
            raise FileNotFoundError("Expected module artifact missing: " + str(path))
        artifacts.append({"path": str(path), "bytes": path.stat().st_size, "sha256": digest(path.read_bytes())})
    recorded = {"recorded_at_utc": stamp(), "run_result": str(run_path), "run_result_sha256": digest(run_path.read_bytes()),
                "exit_codes": {entry["name"]: entry["exit_code"] for entry in result["modules"]},
                "artifacts": artifacts, "validation_scope": "Native compilation and link only; no complete-editor integration or real IWork file conversion claimed"}
    save(EVIDENCE / "remaining-module-artifacts.json", recorded)
    readiness_path = EVIDENCE / "remaining-dependencies-result.json"
    if readiness_path.exists():
        readiness = json.loads(readiness_path.read_text(encoding="utf-8"))
        readiness["additional_module_artifacts"] = str(EVIDENCE / "remaining-module-artifacts.json")
        readiness["integration_pending"] = [
            "HtmlFile2/Fb2File compile Gumbo sources directly",
            "HtmlFile2 compiles md4c sources directly",
            "Real IWork document conversion and complete packaged-editor runtime validation",
        ]
        save(readiness_path, readiness)
    print(json.dumps(recorded, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("acquire", "prepare", "build-hunspell", "smoke-hunspell", "verify", "record-modules"))
    parser.add_argument("--source", choices=list(SOURCES))
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    if args.action == "acquire":
        acquire([args.source] if args.source else list(SOURCES))
    elif args.action == "prepare":
        prepare()
    elif args.action == "build-hunspell":
        build_hunspell()
    elif args.action == "smoke-hunspell":
        smoke_hunspell()
    elif args.action == "verify":
        verify()
    else:
        record_modules()


if __name__ == "__main__":
    main()
