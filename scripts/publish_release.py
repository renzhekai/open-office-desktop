#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Publish an installer to the repository's GitHub Releases and record what was published.

The setup (about 600 MB) is too large for the git repository, so each one goes to its own
release (tag v<version>-dev-<date>), marked as a pre-release. Every release is kept. The
GitHub credential is the one Git Credential Manager already stores for github.com; it is
handed to `gh` in the environment of each call only, never printed or written anywhere.

Checks: the setup's .sha256.txt matches the file; the release does not exist yet; after the
upload, the release's assets have the local names and sizes. A record goes to
evidence/releases/<tag>.json.

Usage:
    python -I scripts/publish_release.py dist/OpenOffice-Setup-0.1.0-dev-20261109-win64.exe --notes notes.md
    python -I scripts/publish_release.py <setup> --notes notes.md --extra dist/OpenOffice-portable-win64-20261106.zip
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "renzhekai/open-office-desktop"
EVIDENCE = ROOT / "evidence" / "releases"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def stored_token():
    query = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                           capture_output=True, text=True, cwd=ROOT, timeout=60)
    for line in query.stdout.splitlines():
        if line.startswith("password="):
            return line[len("password="):]
    sys.exit("no GitHub credential is stored for git; push once with git to store one")


def gh(arguments, token, check=True):
    environment = {**os.environ, "GH_TOKEN": token}
    result = subprocess.run(["gh", *arguments, "--repo", REPOSITORY], capture_output=True, text=True,
                            encoding="utf-8", env=environment, cwd=ROOT, timeout=7200)
    if check and result.returncode != 0:
        sys.exit(f"gh {arguments[0]} {arguments[1]} failed: {result.stderr.strip()[:500]}")
    return result


def checked(path):
    """A file to upload with its .sha256.txt next to it, which must match."""
    path = path if path.is_absolute() else ROOT / path
    record = path.with_name(path.name + ".sha256.txt")
    if not path.is_file() or not record.is_file():
        sys.exit(f"{path} or its .sha256.txt is missing")
    expected = record.read_text(encoding="ascii").split()[0]
    actual = sha256(path)
    if actual != expected:
        sys.exit(f"{path.name}: SHA256 {actual} does not match {record.name}")
    return path, record, actual


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("setup", type=Path)
    parser.add_argument("--notes", type=Path, required=True, help="release notes (Markdown, UTF-8)")
    parser.add_argument("--extra", type=Path, action="append", default=[], help="another file to attach")
    parser.add_argument("--title", help="default: Open Office <version> 开发版 <date>")
    args = parser.parse_args()

    setup, setup_record, setup_sha = checked(args.setup)
    match = re.fullmatch(r"OpenOffice-Setup-(\d+\.\d+\.\d+)-dev-(\d{8}(?:-\d+)?)-win64\.exe", setup.name)
    if not match:
        sys.exit(f"{setup.name} is not named like an Open Office setup")
    version, date = match.groups()
    tag = f"v{version}-dev-{date}"
    title = args.title or f"Open Office {version} 开发版 {date[:4]}-{date[4:6]}-{date[6:8]}{date[8:]}"
    record_path = EVIDENCE / f"{tag}.json"
    if record_path.exists():
        sys.exit(f"{record_path} exists: {tag} was published already")
    files = [(setup, setup_sha), (setup_record, sha256(setup_record))]
    for extra in args.extra:
        path, record, digest = checked(extra)
        files += [(path, digest), (record, sha256(record))]

    token = stored_token()
    if gh(["release", "view", tag], token, check=False).returncode == 0:
        sys.exit(f"release {tag} exists already; refusing to change it")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    started = datetime.datetime.now(datetime.timezone.utc)
    gh(["release", "create", tag, *[str(path) for path, _ in files], "--title", title,
        "--notes-file", str(args.notes if args.notes.is_absolute() else ROOT / args.notes),
        "--prerelease", "--target", commit], token)

    published = json.loads(gh(["release", "view", tag, "--json", "url,assets,tagName,isPrerelease"], token).stdout)
    online = {asset["name"]: asset["size"] for asset in published["assets"]}
    assets = [{"name": path.name, "bytes": path.stat().st_size, "sha256": digest,
               "uploaded_bytes": online.get(path.name)} for path, digest in files]
    ok = all(asset["uploaded_bytes"] == asset["bytes"] for asset in assets)
    record = {"tag": tag, "title": title, "url": published["url"], "prerelease": published["isPrerelease"],
              "commit": commit, "repository": REPOSITORY, "started_utc": started.isoformat(),
              "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "assets": assets, "all_uploaded_intact": ok}
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with open(record_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"tag": tag, "url": published["url"], "all_uploaded_intact": ok}, ensure_ascii=False))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
