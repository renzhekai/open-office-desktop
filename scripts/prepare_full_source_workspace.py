"""Create resumable isolated worktrees for the pinned full native source build.

Does not download, install, compile, clean, or modify the working source files
under upstream. Existing build_tools adaptation is applied only in its new tree.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "build/full-source"
TARGET = AREA / "DesktopEditors"
LOCK = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8"))
ENV = os.environ.copy()
ENV.update(GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0", GIT_NO_LAZY_FETCH="1")


def git(repository, *args):
    command = ["git", "-c", "core.hooksPath=" + str(AREA / "empty-hooks"),
               "-C", str(repository), *args]
    result = subprocess.run(command, env=ENV, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(" ".join(command) + "\n" + result.stdout + result.stderr)
    return result.stdout.strip()


def main():
    if AREA.resolve().parent != (ROOT / "build").resolve():
        raise ValueError("Unexpected isolated workspace")
    AREA.mkdir(exist_ok=True)
    (AREA / "empty-hooks").mkdir(exist_ok=True)
    original = ROOT / LOCK["desktop_editors"]["local_path"]
    repositories = [("DesktopEditors", original, TARGET, LOCK["desktop_editors"]["commit"])]
    repositories += [(name, original / name, TARGET / name, data["commit"])
                     for name, data in LOCK["component_commits"].items()]
    repositories += [("build_tools", ROOT / LOCK["build_tools"]["local_path"],
                      TARGET / "build_tools", LOCK["build_tools"]["commit"])]
    records = []
    for name, source, destination, commit in repositories:
        if git(source, "rev-parse", "HEAD") != commit:
            raise ValueError("Pinned source HEAD changed: " + name)
        if (destination / ".git").exists():
            if git(destination, "rev-parse", "HEAD") != commit:
                raise ValueError("Existing isolated checkout has a different HEAD: " + name)
        else:
            if destination.exists() and any(destination.iterdir()):
                raise FileExistsError("Inspect incomplete destination before resuming: " + str(destination))
            git(source, "worktree", "add", "--detach", str(destination), commit)
        records.append({"name": name, "source": str(source), "worktree": str(destination), "commit": commit})
        (AREA / "worktrees.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print("CHECKOUT " + name + " " + commit, flush=True)
    patch = ROOT / "docs/feasibility/build-tools-windows-toolchain.patch"
    marker = AREA / "initial-toolchain-patch.json"
    if not marker.exists():
        if git(TARGET / "build_tools", "status", "--porcelain", "--untracked-files=no"):
            raise ValueError("Isolated build_tools already changed; inspect before applying patch")
        git(TARGET / "build_tools", "apply", "--check", str(patch))
        git(TARGET / "build_tools", "apply", str(patch))
        marker.write_text(json.dumps({"patch": str(patch), "sha256": hashlib.sha256(patch.read_bytes()).hexdigest()}, indent=2), encoding="utf-8")
    print("READY " + str(TARGET), flush=True)


if __name__ == "__main__":
    main()
