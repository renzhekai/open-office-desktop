"""Run an explicit, resumable sequence of isolated native source builds.

Each invocation has new logs and a machine-readable exit result. This does not
download tools, clean outputs, alter the original sources, or stage the app.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label")
    parser.add_argument("modules", nargs="+")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8:
        parser.error("jobs must be between 1 and 8")
    if any(not re.fullmatch(r"[a-z0-9-]+", x) for x in [args.label, *args.modules]):
        parser.error("labels and module names must be lowercase alphanumeric/hyphen")
    evidence = ROOT / "build/full-source/module-runs" / args.label
    evidence.mkdir(parents=True, exist_ok=False)
    result = {"started_at_utc": datetime.now(timezone.utc).isoformat(),
              "jobs": args.jobs, "modules": [], "status": "running"}
    result_path = evidence / "result.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    save()
    for name in args.modules:
        command = ["cmd.exe", "/d", "/c", "scripts\\build_full_source_core.cmd",
                   name, args.label, str(args.jobs)]
        entry = {"name": name, "command": command,
                 "started_at_utc": datetime.now(timezone.utc).isoformat()}
        result["modules"].append(entry)
        save()
        print("BUILD_STARTED", name, flush=True)
        with (evidence / (name + "-launch.log")).open("wb") as log:
            completed = subprocess.run(command, cwd=ROOT, stdout=log,
                                       stderr=subprocess.STDOUT, check=False)
        entry.update(exit_code=completed.returncode,
                     completed_at_utc=datetime.now(timezone.utc).isoformat())
        print("BUILD_FINISHED", name, completed.returncode, flush=True)
        if completed.returncode:
            result["status"] = "failed"
            save()
            return completed.returncode
        save()
    result["status"] = "passed"
    result["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
    save()
    return 0


if __name__ == "__main__":
    sys.exit(main())
