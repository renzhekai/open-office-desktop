"""Fetch the approved Qt SDK archives; verify bytes before any extraction.

Uses Windows curl with TLS verification and HTTP/1.1. Does not install, execute
downloaded content, accept licenses, change proxy settings, or edit Qt metadata.
Incomplete downloads remain as .part files for a verified later resume.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import hashlib
import json
import subprocess
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "evidence/qt-official-metadata/payload-manifest.json"
CACHE = ROOT / "dependencies/qt-6.11.1-msvc2022-archives"
CURL = Path("C:/Windows/System32/curl.exe")
PACKAGES = {
    "qt.qt6.6111.win64_msvc2022_64",
    "qt.qt6.6111.addons.qtmultimedia.win64_msvc2022_64",
}


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(item):
    name = item["filename"]
    parsed = urlparse(item["url"])
    if (Path(name).name != name or "/" in name or "\\" in name
            or parsed.scheme != "https" or parsed.netloc != "download.qt.io"
            or not parsed.path.endswith("/" + name)
            or item["packageId"] not in PACKAGES):
        raise ValueError("Unexpected archive target or source")
    target = CACHE / name
    part = CACHE / (name + ".part")
    if target.exists():
        if target.stat().st_size != item["size"] or sha256(target) != item["sha256"]:
            raise ValueError("Existing archive differs: " + name)
        return {"module": item["module"], "status": "already_verified"}
    if part.exists() and part.stat().st_size > item["size"]:
        raise ValueError("Partial archive too large: " + name)
    command = [str(CURL), "--http1.1", "--fail", "--location", "--proto", "=https",
               "--proto-redir", "=https", "--connect-timeout", "20", "--max-time", "1800",
               "--speed-limit", "1024", "--speed-time", "90", "--silent", "--show-error"]
    if part.exists() and part.stat().st_size:
        command.extend(["--continue-at", "-"])
    command.extend(["--output", str(part), item["url"]])
    print("Downloading " + item["module"], flush=True)
    process = subprocess.run(command, check=False)
    if process.returncode:
        return {"module": item["module"], "status": "download_failed", "exit_code": process.returncode}
    actual_hash = sha256(part)
    if part.stat().st_size != item["size"] or actual_hash != item["sha256"]:
        return {"module": item["module"], "status": "verification_failed", "sha256": actual_hash}
    part.rename(target)  # Windows refuses replacement of an existing destination.
    print("Verified " + item["module"], flush=True)
    return {"module": item["module"], "status": "downloaded_verified",
            "size": item["size"], "sha256": actual_hash, "path": str(target)}


def main():
    data = json.loads(MANIFEST.read_text(encoding="utf-8-sig"))
    if data["qtVersion"] != "6.11.1" or len(data["archives"]) != 9:
        raise ValueError("Unexpected Qt manifest")
    CACHE.mkdir(parents=True, exist_ok=True)
    results = []
    with ThreadPoolExecutor(max_workers=3) as pool:
        jobs = [pool.submit(fetch, item) for item in data["archives"]]
        for job in as_completed(jobs):
            results.append(job.result())
    print(json.dumps(results, indent=2), flush=True)
    return 0 if all(r["status"] in {"already_verified", "downloaded_verified"} for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
