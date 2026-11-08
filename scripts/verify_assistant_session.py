#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check what an assistant GUI session left behind: saved document, event log and command journal.

The interactive steps are performed separately (desktop control). This script only reads:
- session.json for a clean exit;
- native-events.log for every editor command accepted and completed (no failures or timeouts);
- the profile's assistant-commands.jsonl for the expected commands, sources and result codes, and
  that neither log contains the inserted text (metadata only);
- the saved DOCX: the fixture's paragraphs plus exactly one inserted marker paragraph.
It writes a new evidence JSON and never modifies the inspected files.

Usage:
    python -I scripts/verify_assistant_session.py assistant-ball-session1 \
        --document fixtures/results/assistant-ball-sample.docx --marker "你好，悬浮球助手"
A local AI session (the model proposed the insertion, the person confirmed it):
    python -I scripts/verify_assistant_session.py local-ai-session1 \
        --document fixtures/results/local-ai-sample.docx --marker "..." \
        --editor text.insert --host document.save --from-assistant text.insert
"""

import argparse
import hashlib
import json
import re
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "fixtures" / "generated" / "text-table.docx"
EXPECTED_EDITOR = ["text.insert", "edit.undo", "edit.redo", "selection.getText"]
EXPECTED_HOST = ["document.status", "document.save"]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def paragraphs(path):
    with zipfile.ZipFile(path) as archive:
        xml = archive.read("word/document.xml").decode("utf-8")
    result = []
    for body in re.findall(r"<w:p[ >].*?</w:p>", xml, flags=re.S):
        text = "".join(re.findall(r"<w:t(?: [^>]*)?>([^<]*)</w:t>", body))
        if text:
            result.append(text)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("label")
    parser.add_argument("--document", type=Path, required=True)
    parser.add_argument("--marker", required=True)
    parser.add_argument("--editor", default=",".join(EXPECTED_EDITOR), help="editor commands that must complete")
    parser.add_argument("--host", default=",".join(EXPECTED_HOST), help="host commands that must be journalled")
    parser.add_argument("--from-assistant", default="",
                        help="commands the local AI must have proposed (journal source 'assistant')")
    args = parser.parse_args()
    expected_editor = [name for name in args.editor.split(",") if name]
    expected_host = [name for name in args.host.split(",") if name]
    from_assistant = [name for name in args.from_assistant.split(",") if name]
    session_dir = ROOT / "evidence" / "full-source-gui" / args.label
    output = ROOT / "evidence" / "host-candidate" / f"{args.label}-result.json"
    if output.exists():
        sys.exit(f"{output} exists")
    session = json.loads((session_dir / "session.json").read_text(encoding="utf-8"))
    events = (session_dir / "native-events.log").read_text(encoding="utf-8")
    journal_path = Path(session["profile"]) / "assistant-commands.jsonl"
    journal_text = journal_path.read_text(encoding="utf-8") if journal_path.is_file() else ""
    journal = [json.loads(line) for line in journal_text.splitlines() if line.strip()]
    document = args.document if args.document.is_absolute() else ROOT / args.document

    checks = []

    def check(name, ok, detail):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})

    check("process_exit_zero", session.get("exit_code") == 0, {"exit_code": session.get("exit_code")})

    accepted = re.findall(r"action=editor_command_accepted command=(\S+)", events)
    completed = re.findall(r"action=editor_command_completed command=(\S+)", events)
    failed = re.findall(r"action=editor_command_failed command=(\S+)", events)
    check("editor_commands_reported_back",
          all(name in completed for name in expected_editor) and sorted(accepted) == sorted(completed + failed),
          {"accepted": accepted, "completed": completed, "failed": failed})
    timeouts = [a for a in re.findall(r"action=(\S+)", events) if a in ("operation_timeout", "operation_failed")]
    check("no_document_operation_failed", not timeouts and not failed, {"failures": timeouts + failed})

    names = [entry.get("command") for entry in journal]
    proposed = [entry.get("command") for entry in journal if entry.get("source") == "assistant"]
    check("journal_records_each_command",
          all(name in names for name in expected_editor + expected_host)
          and all(name in proposed for name in from_assistant)
          and all(entry.get("source") == "user"
                  or (entry.get("source") == "assistant" and entry.get("command") in from_assistant)
                  for entry in journal),
          {"entries": journal, "from_assistant": from_assistant})
    check("logs_never_contain_document_text",
          args.marker not in journal_text and args.marker not in events,
          {"journal_bytes": len(journal_text.encode("utf-8")), "events_bytes": len(events.encode("utf-8"))})

    found = paragraphs(document)
    original = paragraphs(FIXTURE)
    remaining = [text for text in found if text != args.marker]
    check("saved_document_has_inserted_paragraph",
          found.count(args.marker) == 1 and remaining == original,
          {"marker": args.marker, "original": original, "found": found})

    report = {
        "checked_at_utc": datetime.now(timezone.utc).isoformat(),
        "session": args.label,
        "runtime": session.get("runtime"),
        "entry_sha256": session.get("entry_sha256"),
        "native_events_sha256": session.get("native_events_sha256"),
        "journal_sha256": hashlib.sha256(journal_text.encode("utf-8")).hexdigest(),
        "document": str(document),
        "document_sha256": sha256(document),
        "checks": checks,
        "all_ok": all(item["ok"] for item in checks),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with open(output, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"all_ok": report["all_ok"], "checks": [(c["check"], c["ok"]) for c in checks]},
                     ensure_ascii=False))
    return 0 if report["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
