"""Read-only Git/source capture. The sole write destination is this archive."""
import concurrent.futures
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
WORK = ROOT / "build/full-source"
MAIN = WORK / "DesktopEditors"
THIRD = MAIN / "core/Common/3dParty"
V8 = THIRD / "v8_89/v8"
ENV = os.environ.copy()
ENV.update(GIT_NO_LAZY_FETCH="1", GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0")
GIT = ["git", "-c", "core.longpaths=true", "-c", "core.quotePath=false"]
DIFF = ["diff", "--binary", "--full-index", "--no-ext-diff", "--no-textconv", "--ignore-submodules=all", "HEAD", "--"]


def now():
    return datetime.now(timezone.utc).isoformat()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_new(relative, data):
    path = OUT / relative
    if not path.resolve().is_relative_to(OUT):
        raise ValueError("Archive output escapes its dedicated directory")
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError("Refusing to replace existing archive file: " + str(path))
    else:
        with path.open("xb") as target:
            target.write(data)
    return {"path": relative.replace("\\", "/"), "size": len(data), "sha256": sha(data)}


def save(relative, value):
    return write_new(relative, (json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode())


def git(path, args, allow_failure=False):
    result = subprocess.run(GIT + ["-C", str(path)] + args, env=ENV, capture_output=True, timeout=180)
    if result.returncode and not allow_failure:
        raise RuntimeError("git " + " ".join(args[:3]) + " at " + str(path) + ": " + result.stderr.decode("utf-8", "replace"))
    return result


def current_file(repository, relative):
    path = repository / relative
    if path.is_symlink():
        value = os.readlink(path).encode("utf-8")
        return {"path": relative, "state": "symlink", "link_target_sha256": sha(value)}
    if not path.resolve().is_relative_to(repository.resolve()):
        raise ValueError("Changed path escapes repository")
    if not path.exists():
        return {"path": relative, "state": "deleted"}
    if not path.is_file():
        return {"path": relative, "state": "nonregular_not_hashed"}
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": relative, "state": "file", "size": path.stat().st_size, "sha256": digest}


def capture(spec):
    name, repository, pin = spec["name"], Path(spec["path"]), spec["pin"]
    if not repository.resolve().is_relative_to(MAIN.resolve()):
        raise ValueError("Repository outside the fixed isolated source tree")
    record = {**spec, "started_at_utc": now()}
    head = git(repository, ["rev-parse", "HEAD"]).stdout.decode().strip()
    record.update(head=head, expected_commit_matches=head == pin)
    if head != pin:
        raise ValueError("Pinned HEAD mismatch: " + name)
    record["origin"] = git(repository, ["remote", "get-url", "origin"]).stdout.decode().strip()
    patch = git(repository, DIFF).stdout
    changed = git(repository, ["diff", "--name-only", "-z", "--no-renames", "--no-ext-diff", "--no-textconv", "--ignore-submodules=all", "HEAD", "--"]).stdout
    paths = [value.decode("utf-8") for value in changed.split(b"\0") if value]
    files = [current_file(repository, relative) for relative in paths]
    record["patch"] = write_new("patches/" + name + ".patch", patch)
    record["head_file"] = write_new("heads/" + name + ".txt", (head + "\n").encode())
    record.update(changed_files=files, changed_file_count=len(files), empty_patch=not patch)
    record["eol_configuration"] = {key: git(repository, ["config", "--get", key], allow_failure=True).stdout.decode().strip() or None for key in ("core.autocrlf", "core.eol")}
    if patch:
        check = git(repository, ["apply", "--reverse", "--check", "--whitespace=nowarn", str(OUT / record["patch"]["path"])], allow_failure=True)
        record["reverse_apply_check"] = {"exit_code": check.returncode, "stderr": check.stderr.decode("utf-8", "replace")}
    # Observe HEAD/diff/file bytes again, without refreshing the index or
    # writing source. Detect a concurrently changing source snapshot.
    if git(repository, ["rev-parse", "HEAD"]).stdout.decode().strip() != head or git(repository, DIFF).stdout != patch:
        raise ValueError("Repository changed during capture: " + name)
    if [current_file(repository, relative) for relative in paths] != files:
        raise ValueError("Changed file bytes moved during capture: " + name)
    record.update(status="captured", stable_during_capture=True, finished_at_utc=now())
    save("repositories/" + name + ".json", record)
    print(name + ": " + str(len(files)) + " changed files, " + str(len(patch)) + " patch bytes", flush=True)
    return record


def specs():
    result = [{"name": "main-" + item["name"], "path": item["worktree"], "pin": item["commit"], "kind": "main_worktree", "receipt": "build/full-source/worktrees.json"} for item in read_json(WORK / "worktrees.json")]
    for key in ("v8", "libxml2"):
        relative = "evidence/full-source-" + key + "-source-result.json"
        item = read_json(ROOT / relative)
        result.append({"name": "dep-" + key, "path": item["destination"], "pin": item["commit"], "kind": "dependency_clone", "receipt": relative})
    for relative, item in read_json(WORK / "v8-evidence/sources.json").items():
        result.append({"name": "dep-v8-" + re.sub(r"[^A-Za-z0-9_-]", "_", relative), "path": str(V8 / relative), "pin": item["commit"], "kind": "dependency_clone", "receipt": "build/full-source/v8-evidence/sources.json"})
    for group in ("graphics", "remaining", "socket"):
        relative = "build/full-source/" + group + "-dependency-evidence/sources.json"
        for name, item in read_json(ROOT / relative).items():
            path = item.get("directory") or str(THIRD / item["relative_path"])
            result.append({"name": "dep-" + group + "-" + name, "path": path, "pin": item["commit"], "kind": "dependency_clone", "receipt": relative})
    if len({Path(item["path"]).resolve() for item in result}) != len(result):
        raise ValueError("Duplicate repository in capture inventory")
    return result


def copy_support():
    receipts = ["sources.lock.json", "build/full-source/worktrees.json", "build/full-source/initial-toolchain-patch.json", "docs/feasibility/build-tools-windows-toolchain.patch", "evidence/full-source-v8-source-result.json", "evidence/full-source-libxml2-source-result.json", "evidence/full-source-openssl-crt-patch.json", "evidence/full-source-libxml-patches.json", "evidence/full-source-agg-cxx17-patch.json", "evidence/full-source-archive-extraction.json", "evidence/full-source-web-build-manifest.json", "evidence/full-source-js-sdk-manifest.json", "evidence/full-source-portable-perl.json"]
    for group in ("graphics", "remaining", "socket"):
        base = "build/full-source/" + group + "-dependency-evidence/"
        receipts.append(base + "sources.json")
        receipts.append(base + ("source-preparation.json" if group == "graphics" else "preparation.json"))
    receipts += ["build/full-source/v8-evidence/" + name for name in ("sources.json", "patches.json", "tools.json", "gn.json", "gn-cipd.json", "deps-audit.json", "dependency-prefix.json", "dependency-prefix-application.json", "dependency-rebuild.json", "build-result.json", "monolith-smoke.json", "README.md")]
    paths = [ROOT / relative for relative in receipts]
    paths += sorted((ROOT / "scripts").glob("*full_source*"))
    copied = []
    for path in paths:
        if not path.is_file():
            copied.append({"source": str(path), "status": "missing_or_not_file"})
            continue
        relative = path.relative_to(ROOT).as_posix()
        contents = path.read_bytes()
        entry = write_new("support/" + relative, contents)
        entry.update(source=relative, status="copied", source_stable_during_copy=sha(path.read_bytes()) == entry["sha256"])
        copied.append(entry)
    # Small generated source inputs are preserved explicitly; this is not an
    # inventory or copy of the full generated build/runtime asset tree.
    overlays = [V8 / "src/base/platform/wrappers.cc", V8 / "build/config/gclient_args.gni", THIRD / "openssl/openssl/Configurations/10-main.conf"]
    for path in overlays:
        relative = path.relative_to(ROOT).as_posix()
        entry = write_new("supplemental/" + relative, path.read_bytes())
        entry.update(source=relative, status="copied", role="selected_non_git_or_generated_source_input")
        copied.append(entry)
    save("support-index.json", copied)
    return copied


def main():
    started = now()
    inventory = specs()
    results, failures = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        work = {pool.submit(capture, spec): spec for spec in inventory}
        for future in concurrent.futures.as_completed(work):
            try:
                results.append(future.result())
            except Exception as error:
                failures.append({"repository": work[future], "error": str(error)})
                print(work[future]["name"] + " FAILED " + str(error), flush=True)
    support = copy_support()
    core = next(item for item in results if item["name"] == "main-core")
    required_line = b"+        PRE_TARGETDEPS += $$CORE_V8_PATH_LIBS/v8_monolith.lib"
    contains_pre_targetdeps = required_line in (OUT / core["patch"]["path"]).read_bytes()
    if not contains_pre_targetdeps:
        failures.append({"repository": "main-core", "error": "Required final V8 PRE_TARGETDEPS line missing from patch"})
    manifest = {"status": "complete" if not failures else "partial", "started_at_utc": started, "finished_at_utc": now(), "source_root": str(MAIN), "archive_root": str(OUT), "scope": "Tracked changes from pinned HEAD, including staged and unstaged changes. Nested repositories captured independently from explicit receipt inventories.", "git_diff_arguments": DIFF, "environment": {key: ENV[key] for key in ("GIT_NO_LAZY_FETCH", "GIT_OPTIONAL_LOCKS", "GIT_TERMINAL_PROMPT")}, "main_worktree_count": sum(item["kind"] == "main_worktree" for item in results), "dependency_clone_count": sum(item["kind"] == "dependency_clone" for item in results), "tracked_changed_file_count": sum(item["changed_file_count"] for item in results), "nonempty_patch_count": sum(not item["empty_patch"] for item in results), "required_core_v8_pre_targetdeps_present": contains_pre_targetdeps, "repositories": sorted(results, key=lambda item: item["name"]), "support_index": "support-index.json", "copied_support_file_count": sum(item["status"] == "copied" for item in support), "failures": failures, "not_covered": ["Untracked and ignored files are not exhaustively scanned or included in Git patches.", "Generated SDKJS/web bundles, qmake/GN/Ninja/CMake outputs, node_modules, compiled objects/libraries/executables and runtime stage assets are not bundled.", "Archive-based Boost/ICU/OpenSSL sources are not Git repositories in this capture; existing acquisition/extraction/configuration drivers and receipts are indexed. The selected final OpenSSL configuration is copied separately.", "External dependencies/tools, their complete original archives, CEF resources, fonts and dictionaries remain supplied by existing fixed source/asset manifests and acquisition scripts.", "The owning application repository's own untracked edits are outside the eight upstream worktrees; relevant full_source driver scripts are copied for reference, not represented as an application Git patch.", "Reverse apply checks validate patches against current changed files. No pristine-checkout replay or rebuild was performed for this archive."]}
    save("manifest.json", manifest)
    print(json.dumps({key: manifest[key] for key in ("status", "main_worktree_count", "dependency_clone_count", "tracked_changed_file_count", "nonempty_patch_count", "required_core_v8_pre_targetdeps_present")}), flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
