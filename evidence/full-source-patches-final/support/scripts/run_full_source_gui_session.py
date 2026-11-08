"""Launch the new source-core host, retain logs, and record its real exit.

The interactive document actions are performed and observed separately via CUA.
This launcher never edits the document or claims a successful round trip.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "build/full-source-run"


def stamp():
    return datetime.now(timezone.utc).isoformat()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label")
    parser.add_argument("--document", type=Path)
    parser.add_argument("--exit-after-ms", type=int)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9-]+", args.label):
        parser.error("label must contain lowercase alphanumeric/hyphen")
    manifest = json.loads((RUNTIME / "openoffice-full-source-stage.json").read_text(encoding="utf-8"))
    executable = RUNTIME / "OpenOffice.exe"
    host_record = next(item for item in manifest["files"] if item["destination"] == "OpenOffice.exe")
    if hashlib.sha256(executable.read_bytes()).hexdigest() != host_record["sha256"]:
        raise RuntimeError("Staged host is not the recorded new source-build entry")
    profile = ROOT / "build/full-source-profiles" / args.label
    if profile.exists():
        raise FileExistsError("A fresh profile is required: " + str(profile))
    output = ROOT / "evidence/full-source-gui" / args.label
    output.mkdir(parents=True, exist_ok=False)
    command = [str(executable), "--engine-profile", str(profile),
               "--smoke-report", str(output / "launch-report.json")]
    if args.document:
        document = args.document.resolve(strict=True)
        if not document.is_relative_to((ROOT / "fixtures/results").resolve()):
            raise ValueError("GUI regression only opens the project's result fixture copies")
        command.extend(["--open", str(document)])
    if args.exit_after_ms is not None:
        if not 600 <= args.exit_after_ms <= 60000:
            parser.error("exit-after-ms must be between 600 and 60000")
        command.extend(["--exit-after-ms", str(args.exit_after_ms)])
    env = os.environ.copy()
    for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH"):
        env.pop(name, None)
    env["QT_QPA_PLATFORM"] = "windows"
    env["PATH"] = os.pathsep.join([str(RUNTIME), str(Path(os.environ["SystemRoot"]) / "System32")])
    result = {"started_at_utc": stamp(), "command": command,
              "profile": str(profile), "profile_existed_before": False,
              "entry_sha256": host_record["sha256"],
              "stage_manifest_sha256": hashlib.sha256((RUNTIME / "openoffice-full-source-stage.json").read_bytes()).hexdigest(),
              "status": "launching", "document_actions_verified": False,
              "roundtrip_verified": False}
    report = output / "session.json"
    with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
        process = subprocess.Popen(command, cwd=RUNTIME, env=env, stdout=stdout, stderr=stderr)
        result.update(pid=process.pid, status="running")
        report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print("SOURCE_GUI_STARTED", process.pid, str(report), flush=True)
        code = process.wait()
    result.update(status="exited", exit_code=code, exited_at_utc=stamp())
    event_log = profile / "native-events.log"
    if event_log.is_file():
        data = event_log.read_bytes()
        (output / "native-events.log").write_bytes(data)
        result["native_events_sha256"] = hashlib.sha256(data).hexdigest()
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("SOURCE_GUI_EXITED", code, flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
