#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check what an office-AI GUI session left behind (read only; writes a new evidence JSON).

The session (run_full_source_gui_session.py on fixtures/results/office-ai-sample.docx) is
driven by hand or desktop control: whole-document proofreading with "全部改正", a selection
polished and put back with "替换原文", optionally a draft inserted with "插入文档", then /保存.
Checks:
- clean exit;
- every editor command the editor accepted also reported back, none failed or timed out;
- the command journal has the AI-proposed edits (source "assistant") and the person's save;
- neither log contains document text;
- the saved DOCX: both typos fixed, the untouched paragraphs kept, the polished paragraph
  changed; when a draft was inserted, at least one heading style is used.

Usage:
    python -I scripts/verify_office_ai_session.py office-ai-session1 --document fixtures/results/office-ai-sample.docx
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
TITLE = "关于开展安全培训的通知"
POLISH_SOURCE = "大家要带好笔计本，认真听讲，会后每人要交一份心得体会给行政部，下周三之前交。"
SECRETS = ["心得体会", "安全意识", "三楼会议室"]  # document text that must never reach a log


def paragraphs(xml):
    result = []
    for body in re.findall(r"<w:p[ >].*?</w:p>", xml, flags=re.S):
        text = "".join(re.findall(r"<w:t(?: [^>]*)?>([^<]*)</w:t>", body))
        style = re.search(r'<w:pStyle w:val="([^"]+)"', body)
        if text:
            result.append({"text": text, "style": style.group(1) if style else None})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("label")
    parser.add_argument("--document", type=Path, required=True)
    args = parser.parse_args()
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
    with zipfile.ZipFile(document) as archive:
        xml = archive.read("word/document.xml").decode("utf-8")
    found = paragraphs(xml)
    texts = [p["text"] for p in found]
    joined = "\n".join(texts)

    checks = []

    def check(name, ok, detail):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})

    check("process_exit_zero", session.get("exit_code") == 0, {"exit_code": session.get("exit_code")})
    accepted = re.findall(r"action=editor_command_accepted command=(\S+)", events)
    completed = re.findall(r"action=editor_command_completed command=(\S+)", events)
    failed = re.findall(r"action=editor_command_failed command=(\S+)", events)
    check("editor_commands_reported_back", accepted and sorted(accepted) == sorted(completed + failed),
          {"accepted": len(accepted), "completed": len(completed), "failed": failed})
    timeouts = [a for a in re.findall(r"action=(\S+)", events) if a in ("operation_timeout", "operation_failed")]
    check("no_document_operation_failed", not timeouts, {"failures": timeouts})

    def entries(command, source):
        return [e for e in journal if e.get("command") == command and e.get("source") == source]

    replaced = [e for e in entries("document.replaceText", "assistant") if e.get("ok")]
    polished = [e for e in entries("selection.replace", "assistant") if e.get("ok")]
    saved = [e for e in entries("document.save", "user") if e.get("ok")]
    reads = entries("document.getText", "assistant") + entries("selection.getText", "assistant")
    inserted = [e for e in entries("text.insert", "assistant") if e.get("ok")]
    check("journal_has_ai_edits_and_user_save",
          len(replaced) >= 2 and len(polished) >= 1 and len(saved) >= 1 and len(reads) >= 2,
          {"replaceText_ok": len(replaced), "selection_replace_ok": len(polished), "save_ok": len(saved),
           "reads": len(reads), "insert_ok": len(inserted), "entries": len(journal)})
    leaked = [s for s in SECRETS if s in journal_text or s in events]
    check("logs_never_contain_document_text", not leaked, {"leaked": leaked})

    check("typos_fixed", "按排" not in joined and "笔计本" not in joined and "安排" in joined,
          {"has_按排": "按排" in joined, "has_笔计本": "笔计本" in joined, "has_安排": "安排" in joined})
    check("untouched_paragraphs_kept",
          texts[:2] == [TITLE, "各部门："] and "行政部" in texts and any(t.startswith("为了提高大家的安全意识") for t in texts),
          {"first": texts[:2], "has_signature": "行政部" in texts})
    fixed_source = POLISH_SOURCE.replace("笔计本", "笔记本")
    check("selection_was_polished", POLISH_SOURCE not in texts and fixed_source not in texts,
          {"paragraphs": texts})
    headings = [p for p in found if p["style"] and p["text"] not in (TITLE,)]
    check("draft_has_real_headings_if_inserted", not inserted or bool(headings),
          {"inserted": len(inserted), "styled_paragraphs": headings[:6]})

    report = {
        "checked_at_utc": datetime.now(timezone.utc).isoformat(),
        "session": args.label,
        "runtime": session.get("runtime"),
        "entry_sha256": session.get("entry_sha256"),
        "journal_sha256": hashlib.sha256(journal_text.encode("utf-8")).hexdigest(),
        "document": str(document),
        "document_sha256": hashlib.sha256(document.read_bytes()).hexdigest(),
        "paragraphs": found,
        "checks": checks,
        "all_ok": all(item["ok"] for item in checks),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with open(output, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"all_ok": report["all_ok"], "checks": [(c["check"], c["ok"]) for c in checks]}, ensure_ascii=False))
    return 0 if report["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
