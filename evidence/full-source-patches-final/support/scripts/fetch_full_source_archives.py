"""Fetch and verify the three pinned source archives; never install or execute them."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / "dependencies/full-source"
MANIFEST = DIRECTORY / "source-manifest.json"


def digest(path, algorithm):
    with path.open("rb") as source:
        return hashlib.file_digest(source, algorithm).hexdigest()


def fetch(source):
    target = DIRECTORY / source["filename"]
    partial = target.with_name(target.name + ".part")
    algorithm, expected = source["expected_hash_algorithm"], source["expected_hash"]
    if not expected:
        raise ValueError("Missing publisher checksum: " + source["name"])
    if target.exists():
        if digest(target, algorithm) != expected:
            raise ValueError("Existing archive does not match: " + str(target))
    else:
        if not (partial.exists() and digest(partial, algorithm) == expected):
            command = ["curl.exe", "--fail", "--location", "--http1.1", "--retry", "3",
                       "--connect-timeout", "20", "--max-time", "1800", "--continue-at", "-",
                       "--output", str(partial), source["url"]]
            with (DIRECTORY / (source["name"] + "-download.log")).open("a", encoding="utf-8") as log:
                result = subprocess.run(command, stdout=log, stderr=log)
            if result.returncode:
                return {**source, "download_status": "failed", "curl_exit_code": result.returncode}
        if digest(partial, algorithm) != expected:
            raise ValueError("Publisher checksum mismatch: " + str(partial))
        if source.get("expected_size_bytes") and partial.stat().st_size != source["expected_size_bytes"]:
            raise ValueError("Published archive size mismatch: " + str(partial))
        partial.rename(target)
    return {**source, "actual_size_bytes": target.stat().st_size,
            "actual_sha256": digest(target, "sha256"), "download_status": "verified",
            "checksum_status": "downloaded_bytes_match_publisher_checksum", "local_path": str(target)}


def main():
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    checksum_text = (DIRECTORY / "icu-SHASUM512.txt.part").read_text(encoding="utf-8")
    match = re.search(r"^([0-9a-f]{128})\s+icu4c-74_2-src\.tgz\s*$", checksum_text, re.MULTILINE)
    if not match:
        raise ValueError("ICU archive checksum not found in official SHA512 listing")
    for source in data["sources"]:
        if source["name"] == "icu4c":
            source["expected_hash"] = match.group(1)
            source["checksum_status"] = "publisher_checksum_downloaded"
    data["status"] = "downloading_source_archives"
    data["blocker"] = None
    MANIFEST.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = {pool.submit(fetch, source): index for index, source in enumerate(data["sources"])}
        for future in concurrent.futures.as_completed(futures):
            index = futures[future]
            data["sources"][index] = future.result()
            MANIFEST.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            print(data["sources"][index]["name"] + ": " + data["sources"][index]["download_status"], flush=True)
    passed = all(x["download_status"] == "verified" for x in data["sources"])
    data["status"] = "source_archives_verified" if passed else "source_download_incomplete"
    data["download_extraction_installation_or_build_performed"] = any(x["download_status"] == "verified" for x in data["sources"])
    data["extraction_installation_or_build_performed"] = False
    MANIFEST.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
