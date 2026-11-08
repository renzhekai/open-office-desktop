"""Read-only inventory of explicitly scoped ONLYOFFICE local inputs.

No downloads, builds, application launches, upstream scripts, or installation.
Git is used only for local revision/submodule metadata. A file's presence is not
evidence of compatibility, integration, or a successful editor round trip.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[1]
COMMIT_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")
MAX_SCAN_ENTRIES = 100_000
MAX_SAMPLE_PATHS = 30
MAX_SCAN_DEPTH = 10
EXCLUDED_DIRECTORIES = {".git", "node_modules", ".venv", "venv", "__pycache__"}
MARKERS = {
    "desktop_editors": ("README.md", "LICENSE", ".gitmodules"),
    "build_tools": ("README.md", "LICENSE", "configure.py", "make.py"),
}
COMPONENT_DIRECTORIES = ("core", "desktop-apps", "desktop-sdk", "sdkjs", "web-apps")


def is_link_or_reparse(path: Path) -> bool:
    """Never follow symlinks/junctions while inventorying directory contents."""
    attributes = path.lstat()
    return stat.S_ISLNK(attributes.st_mode) or bool(
        getattr(attributes, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    )


def issue(code: str, path: Path | str, detail: str) -> dict[str, str]:
    return {"code": code, "path": str(path), "detail": detail}


def git_read(root: Path, arguments: list[str]) -> dict[str, Any]:
    # Disable process-local filesystem-monitor hooks; neither this nor
    # GIT_OPTIONAL_LOCKS changes Git configuration on disk.
    command = ["git", "--no-optional-locks", "-c", "core.fsmonitor=false", "-C", str(root), *arguments]
    result: dict[str, Any] = {"command": command}
    if shutil.which("git") is None:
        return {**result, "returncode": None, "error": "Git executable is not available on PATH"}
    environment = os.environ.copy()
    environment["GIT_TERMINAL_PROMPT"] = "0"
    environment["GIT_OPTIONAL_LOCKS"] = "0"
    try:
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                   errors="replace", timeout=30, env=environment, check=False)
        result.update(returncode=completed.returncode, stdout=completed.stdout.rstrip("\r\n"),
                      stderr=completed.stderr.rstrip("\r\n"))
    except (OSError, subprocess.TimeoutExpired) as exc:
        result.update(returncode=None, error=str(exc))
    return result


def presence_inventory(root: Path) -> dict[str, Any]:
    categories = {name: {"count": 0, "sample_paths": []} for name in
                  ("exe", "dll", "library", "editor_resource_candidates")}
    known_names = {name: [] for name in ("DesktopEditors.exe", "x2t.exe", "libcef.dll",
                                       "ascdocumentscore.dll", "qtascdocumentscore.dll")}
    errors: list[str] = []
    skipped_links: list[str] = []
    depth_limited: list[str] = []
    entries_seen = 0
    truncated = False

    def walk_error(error: OSError) -> None:
        errors.append(str(error))

    for directory, directories, filenames in os.walk(root, topdown=True, onerror=walk_error, followlinks=False):
        relative_directory = Path(directory).relative_to(root)
        depth = len(relative_directory.parts)
        kept: list[str] = []
        for name in sorted(directories):
            candidate = Path(directory) / name
            if name in EXCLUDED_DIRECTORIES:
                continue
            try:
                if is_link_or_reparse(candidate):
                    skipped_links.append(str(candidate.relative_to(root)))
                elif depth >= MAX_SCAN_DEPTH:
                    depth_limited.append(str(candidate.relative_to(root)))
                else:
                    kept.append(name)
            except OSError as exc:
                errors.append(str(exc))
        directories[:] = kept
        for name in sorted(filenames):
            entries_seen += 1
            if entries_seen > MAX_SCAN_ENTRIES:
                truncated = True
                break
            candidate = Path(directory) / name
            try:
                if is_link_or_reparse(candidate):
                    skipped_links.append(str(candidate.relative_to(root)))
                    continue
            except OSError as exc:
                errors.append(str(exc))
                continue
            relative = candidate.relative_to(root)
            suffix = candidate.suffix.lower()
            category = {".exe": "exe", ".dll": "dll", ".lib": "library", ".a": "library",
                        ".so": "library", ".dylib": "library"}.get(suffix)
            if category:
                categories[category]["count"] += 1
                if len(categories[category]["sample_paths"]) < MAX_SAMPLE_PATHS:
                    categories[category]["sample_paths"].append(str(relative))
            if any(part.lower() in {"sdkjs", "web-apps", "editors"} for part in relative.parts) and suffix in {
                    ".js", ".html", ".css", ".wasm", ".bin", ".json"}:
                entry = categories["editor_resource_candidates"]
                entry["count"] += 1
                if len(entry["sample_paths"]) < MAX_SAMPLE_PATHS:
                    entry["sample_paths"].append(str(relative))
            for known_name in known_names:
                if name.lower() == known_name.lower() and len(known_names[known_name]) < MAX_SAMPLE_PATHS:
                    known_names[known_name].append(str(relative))
        if truncated:
            break
    return {
        "meaning": "File/directory presence only; names and suffixes do not establish format, ABI, compatibility or integration.",
        "scope": str(root), "categories": categories, "known_runtime_names": known_names,
        "files_examined": min(entries_seen, MAX_SCAN_ENTRIES),
        "entry_limit": MAX_SCAN_ENTRIES, "depth_limit": MAX_SCAN_DEPTH,
        "sample_limit_per_category": MAX_SAMPLE_PATHS,
        "excluded_directory_names": sorted(EXCLUDED_DIRECTORIES),
        "skipped_links_or_reparse_points": skipped_links,
        "depth_limited_paths": depth_limited, "entry_limit_reached": truncated,
        "errors": errors,
        "scan_complete_within_policy": not (errors or truncated or depth_limited or skipped_links),
    }


def inspect_root(root: Path, kind: str, expected_commit: str | None,
                 locked_components: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, str]]]:
    result: dict[str, Any] = {"path": str(root), "kind": kind, "exists": False,
                              "expected_commit": expected_commit, "revision_verified": False}
    issues: list[dict[str, str]] = []
    try:
        if not root.exists():
            issues.append(issue("missing_root", root, "The selected local input directory does not exist"))
            return result, issues
        result["exists"] = True
        if not root.is_dir():
            issues.append(issue("not_a_directory", root, "The selected local input must be a directory"))
            return result, issues
        if is_link_or_reparse(root):
            issues.append(issue("linked_root_not_followed", root, "Root symlink/junction/reparse point was not followed"))
            return result, issues
        result["presence"] = presence_inventory(root)
        if not result["presence"]["scan_complete_within_policy"]:
            issues.append(issue("presence_scan_incomplete", root, "See inventory limits, skipped links and errors"))
        if kind == "runtime":
            result["revision_check"] = "not_applicable_to_binary_directory"
            if not result["presence"]["known_runtime_names"]["DesktopEditors.exe"]:
                issues.append(issue("runtime_executable_not_found", root, "DesktopEditors.exe not found within the bounded inventory"))
            return result, issues
        result["required_markers"] = []
        for name in MARKERS[kind]:
            candidate = root / name
            present = candidate.is_file() and not is_link_or_reparse(candidate)
            result["required_markers"].append({"path": str(candidate), "present": present})
            if not present:
                issues.append(issue("missing_required_marker", candidate, "Expected source marker file is absent or linked"))
        if kind == "desktop_editors":
            result["component_directories"] = [
                {"path": str(root / name), "present": (root / name).is_dir()}
                for name in COMPONENT_DIRECTORIES
            ]
        valid_expected_commit = isinstance(expected_commit, str) and bool(COMMIT_PATTERN.fullmatch(expected_commit))
        if not valid_expected_commit:
            issues.append(issue("invalid_locked_commit", root, "sources.lock.json has no valid 40-character commit for this repository"))
        if not (root / ".git").exists():
            issues.append(issue("not_a_local_repository", root, "No .git directory/file at this exact root; parent repositories are not used"))
            return result, issues
        git_checks = [git_read(root, ["rev-parse", "--show-toplevel"])]
        result["git_checks"] = git_checks
        top = git_checks[0]
        if top["returncode"] != 0:
            issues.append(issue("git_root_check_failed", root, "Git could not identify the selected repository root; see captured result"))
            return result, issues
        if Path(top.get("stdout", "")).resolve() != root.resolve():
            issues.append(issue("git_root_mismatch", root, "Git resolved a different repository root"))
            return result, issues
        revision = git_read(root, ["rev-parse", "HEAD"])
        git_checks.append(revision)
        if revision["returncode"] != 0:
            issues.append(issue("git_head_check_failed", root, "Git could not read HEAD; see captured result"))
        else:
            result["actual_commit"] = revision.get("stdout", "")
            result["revision_verified"] = bool(valid_expected_commit and result["actual_commit"].lower() == expected_commit.lower())
            if not result["revision_verified"]:
                issues.append(issue("commit_mismatch", root, "Local HEAD does not match sources.lock.json"))
        if not result["presence"]["scan_complete_within_policy"]:
            result["submodules"] = []
            result["submodule_status"] = "not_checked_incomplete_inventory"
            issues.append(issue("recursive_git_check_skipped", root, "Recursive Git traversal was skipped because the inventory could not establish an unlinked, fully inspected directory scope"))
            return result, issues
        submodule_command = git_read(root, ["submodule", "status", "--recursive"])
        git_checks.append(submodule_command)
        result["submodules"] = []
        if submodule_command["returncode"] != 0:
            issues.append(issue("git_submodule_check_failed", root, "Git could not read recursive submodule status; see captured result"))
        else:
            for line in submodule_command.get("stdout", "").splitlines():
                match = re.match(r"^([-+ U])([0-9a-fA-F]{40}) (.+)$", line)
                if not match:
                    issues.append(issue("unparsed_submodule_status", root, "Unrecognized status line: " + line))
                    continue
                prefix, commit, rest = match.groups()
                submodule_path = rest.rsplit(" (", 1)[0]
                state = {" ": "matches_superproject", "-": "uninitialized", "+": "commit_differs", "U": "conflict"}[prefix]
                result["submodules"].append({"path": submodule_path, "commit": commit, "status": state, "raw": line})
                if prefix != " ":
                    issues.append(issue("submodule_" + state, root / submodule_path, "Submodule is not initialized at the superproject-recorded commit"))
            if kind == "desktop_editors":
                by_path = {item["path"]: item for item in result["submodules"]}
                for name, component in locked_components.items():
                    if not isinstance(component, dict) or not isinstance(component.get("commit"), str):
                        issues.append(issue("invalid_component_lock", root / name, "Invalid component commit record in sources.lock.json"))
                        continue
                    if name not in by_path:
                        issues.append(issue("locked_component_not_reported", root / name, "Locked component is absent from recursive submodule status"))
                    elif by_path[name]["commit"].lower() != component["commit"].lower():
                        issues.append(issue("locked_component_mismatch", root / name, "Reported submodule commit does not match its additional component lock"))
    except (OSError, ValueError) as exc:
        issues.append(issue("inspection_error", root, str(exc)))
    return result, issues


def check_inputs(source_root: Path, build_tools_root: Path, runtime_root: Path | None = None,
                 lock_file: Path = PROJECT_ROOT / "sources.lock.json") -> tuple[dict[str, Any], int]:
    issues: list[dict[str, str]] = []
    try:
        lock = json.loads(lock_file.read_text(encoding="utf-8"))
        if not isinstance(lock, dict):
            raise ValueError("Lock file must be a JSON object")
    except (OSError, ValueError) as exc:
        lock = {}
        issues.append(issue("lock_file_unavailable_or_invalid", lock_file, str(exc)))
    roots: dict[str, Any] = {}
    for kind, path in (("desktop_editors", source_root), ("build_tools", build_tools_root)):
        section = lock.get(kind, {})
        expected_commit = section.get("commit") if isinstance(section, dict) else None
        components = lock.get("component_commits", {})
        if not isinstance(components, dict):
            components = {}
            issues.append(issue("invalid_component_locks", lock_file, "component_commits must be an object"))
        result, problems = inspect_root(Path(os.path.abspath(path)), kind, expected_commit, components)
        roots[kind] = result
        issues.extend(problems)
    if runtime_root is not None:
        result, problems = inspect_root(Path(os.path.abspath(runtime_root)), "runtime", None, {})
        roots["runtime"] = result
        issues.extend(problems)
    else:
        roots["runtime"] = {"status": "not_requested", "reason": "No --runtime-root supplied; no installed-application or personal-directory search performed"}
    return {
        "schema_version": 1, "checked_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "inputs_missing_or_issues_found" if issues else "local_presence_and_revision_checks_passed",
        "lock_file": str(lock_file), "roots": roots, "issues": issues,
        "integration_status": "not_verified", "roundtrip_status": "not_executed",
        "upstream_code_executed": False, "download_performed": False,
        "limitations": [
            "Only the selected roots are inventoried; no default search outside the two project upstream roots.",
            "Presence and recorded HEAD/submodule revisions do not verify working-tree modifications, dependency compatibility, ABI, licenses, buildability or editor integration.",
            "No executable/library/editor resource was loaded, and no upstream build script was run.",
            "Runtime inventory is optional and only reports filenames; no runtime checksum/version or execution verification is performed.",
        ],
    }, 2 if issues else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=PROJECT_ROOT / "upstream" / "DesktopEditors")
    parser.add_argument("--build-tools-root", type=Path, default=PROJECT_ROOT / "upstream" / "DesktopEditors" / "build_tools")
    parser.add_argument("--runtime-root", type=Path)
    parser.add_argument("--output", type=Path, help="Write one new JSON file; existing targets are never overwritten")
    args = parser.parse_args(argv)
    if args.output is not None and (args.output.exists() or args.output.is_symlink()):
        report, code = {"status": "output_refused", "path": str(args.output.absolute()),
                        "reason": "Refusing to overwrite an existing output file", "upstream_code_executed": False}, 2
    else:
        report, code = check_inputs(args.source_root, args.build_tools_root, args.runtime_root)
        if args.output is not None:
            try:
                # Exclusive creation also guards against another writer appearing
                # after the existence check. Parent directories are not created.
                with args.output.open("x", encoding="utf-8", newline="\n") as stream:
                    json.dump(report, stream, ensure_ascii=False, indent=2)
                    stream.write("\n")
            except OSError as exc:
                report = {"status": "output_write_failed", "path": str(args.output.absolute()),
                          "reason": str(exc), "check_result": report}
                code = 2
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return code


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
