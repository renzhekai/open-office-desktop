#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Download the user-approved built-in AI engine files and test models, and verify them.

Approved by the user on 2026-11-08, item by item:
- llama.cpp b12202 official Windows builds from GitHub: Vulkan x64 and CPU x64;
- Qwen official GGUF test models from ModelScope: Qwen3-1.7B-Q8_0 and Qwen3-4B-Q4_K_M.

Everything goes under D:/open office/dependencies (C is nearly full), never into the system,
never onto PATH. Each file is downloaded to "<name>.part" (resumable), checked against the
publisher's SHA256 (GitHub release asset digest / ModelScope file Sha256), then renamed.
A verified file is never downloaded again or overwritten. The zips are extracted into new,
separate folders (path traversal refused). A JSON record goes to evidence/local-ai-engine/.

Usage:
    python -I scripts/fetch_local_ai_dependencies.py [engine] [models]
"""

import hashlib
import json
import sys
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LLAMA_TAG = "b12202"
LLAMA_ASSETS = [f"llama-{LLAMA_TAG}-bin-win-vulkan-x64.zip", f"llama-{LLAMA_TAG}-bin-win-cpu-x64.zip"]
MODELS = [("Qwen/Qwen3-1.7B-GGUF", "Qwen3-1.7B-Q8_0.gguf"), ("Qwen/Qwen3-4B-GGUF", "Qwen3-4B-Q4_K_M.gguf")]
ENGINE_DIR = ROOT / "dependencies" / f"llama.cpp-{LLAMA_TAG}"
MODEL_DIR = ROOT / "dependencies" / "local-models"
EVIDENCE = ROOT / "evidence" / "local-ai-engine"
USER_AGENT = "open-office-dependency-fetch"


def get_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, target, expected_sha256, expected_size):
    if target.exists():
        if sha256(target) != expected_sha256:
            sys.exit(f"{target} exists but does not match the published SHA256; not touching it")
        print("already verified", target.name, flush=True)
        return
    part = target.with_name(target.name + ".part")
    have = part.stat().st_size if part.exists() else 0
    headers = {"User-Agent": USER_AGENT}
    if have:
        headers["Range"] = f"bytes={have}-"
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=120) as response:
        if have and response.status != 206:
            have = 0  # the server ignored the range: start over
        with open(part, "ab" if have else "wb") as handle:
            done = have
            last = 0
            while True:
                block = response.read(1 << 20)
                if not block:
                    break
                handle.write(block)
                done += len(block)
                if done - last >= 200 << 20:
                    last = done
                    print(f"  {target.name}: {done / 2**20:.0f} / {expected_size / 2**20:.0f} MB", flush=True)
    actual = sha256(part)
    if part.stat().st_size != expected_size or actual != expected_sha256:
        sys.exit(f"{part} does not match the published size/SHA256 ({actual}); kept as .part for inspection")
    part.rename(target)
    print("verified", target.name, flush=True)


def extract(archive, destination):
    if destination.exists():
        print("already extracted", destination.name, flush=True)
        return
    destination.mkdir(parents=True)
    base = destination.resolve()
    with zipfile.ZipFile(archive) as zipped:
        for member in zipped.infolist():
            path = (destination / member.filename).resolve()
            if not path.is_relative_to(base):
                sys.exit(f"refusing archive entry outside the folder: {member.filename}")
        zipped.extractall(destination)
    print("extracted", destination.name, flush=True)


def main():
    parts = set(sys.argv[1:]) or {"engine", "models"}
    record = {"kind": "local-ai-engine-dependencies", "approved": "2026-11-08, per item, in chat",
              "started_utc": datetime.now(timezone.utc).isoformat(), "files": []}
    if "engine" in parts:
        release = get_json(f"https://api.github.com/repos/ggml-org/llama.cpp/releases/tags/{LLAMA_TAG}")
        assets = {asset["name"]: asset for asset in release["assets"]}
        download_dir = ENGINE_DIR / "download"
        download_dir.mkdir(parents=True, exist_ok=True)
        for name in LLAMA_ASSETS:
            asset = assets[name]
            digest = asset.get("digest") or ""
            if not digest.startswith("sha256:"):
                sys.exit(f"{name}: no published SHA256 digest")
            target = download_dir / name
            download(asset["browser_download_url"], target, digest.split(":", 1)[1], asset["size"])
            folder = ENGINE_DIR / name.removeprefix(f"llama-{LLAMA_TAG}-bin-").removesuffix(".zip")
            extract(target, folder)
            record["files"].append({"name": name, "source": asset["browser_download_url"], "bytes": asset["size"],
                                    "sha256": digest.split(":", 1)[1], "extracted_to": str(folder)})
    if "models" in parts:
        download_dir = MODEL_DIR / "download"
        download_dir.mkdir(parents=True, exist_ok=True)
        for repo, name in MODELS:
            listing = get_json(f"https://www.modelscope.cn/api/v1/models/{repo}/repo/files?Revision=master&Recursive=true")
            entry = next(f for f in listing["Data"]["Files"] if f["Name"] == name)
            url = f"https://www.modelscope.cn/models/{repo}/resolve/master/{name}"
            target = download_dir / name
            download(url, target, entry["Sha256"], entry["Size"])
            record["files"].append({"name": name, "source": url, "bytes": entry["Size"], "sha256": entry["Sha256"],
                                    "path": str(target)})
    record["finished_utc"] = datetime.now(timezone.utc).isoformat()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    with open(EVIDENCE / f"fetch-{'-'.join(sorted(parts))}-{stamp}.json", "x", encoding="utf-8", newline="\n") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"ok": True, "files": [f["name"] for f in record["files"]]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
