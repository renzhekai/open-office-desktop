#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check the files and native event log of a New / Save As / same-process replacement GUI session.

The interactive steps are performed separately (desktop control); this script only reads what the
session left behind: each saved DOCX must contain exactly the paragraphs typed before it was saved,
the replaced source document must be byte-identical to its fixture (its edit was discarded), and the
native event log must show the replaced views being retired and destroyed while the process kept
running to a clean exit. It writes a new evidence JSON and never modifies the inspected files.

Usage:
    python -I scripts/verify_host_saveas_session.py host-saveas-session1
    python -I scripts/verify_host_saveas_session.py host-saveas-r2-recheck --prefix host-saveas-r2 \
        --same-path-marker "原路径保存验证" --close-discarded "关闭时放弃"
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
RESULTS = ROOT / "fixtures" / "results"
FIRST = "新建文档验证 2026-11-06"
SECOND = "另存为验证 第二段"
EDITOR = "编辑器内保存验证"
DISCARDED = "放弃的修改"
FIXTURE = ROOT / "fixtures" / "generated" / "text-table.docx"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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
    parser.add_argument("--prefix", default="host-saveas", help="file name prefix of this session's result files")
    parser.add_argument("--same-path-marker",
                        help="paragraph added to the opened document and saved to its own path; without it the "
                             "opened document must stay byte-identical to its fixture")
    parser.add_argument("--close-discarded",
                        help="text typed before closing with 不保存; must be absent from the editor-saved file")
    args = parser.parse_args()
    # (file, paragraphs that must be present, texts that must be absent)
    expected = [
        (f"{args.prefix}-new.docx", [FIRST], [SECOND]),
        (f"{args.prefix}-copy.docx", [FIRST, SECOND], []),
        (f"{args.prefix}-editor.docx", [EDITOR], [FIRST, SECOND] + ([args.close_discarded] if args.close_discarded else [])),
    ]
    source_name = f"{args.prefix}-open-source.docx"
    session_dir = ROOT / "evidence" / "full-source-gui" / args.label
    output = ROOT / "evidence" / "host-candidate" / f"{args.label}-result.json"
    if output.exists():
        sys.exit(f"{output} exists")
    session = json.loads((session_dir / "session.json").read_text(encoding="utf-8"))
    log = (session_dir / "native-events.log").read_text(encoding="utf-8")
    actions = re.findall(r" action=(\S+) event=(-?\d+) view=(-?\d+)", log)

    checks = []

    def check(name, ok, detail):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})

    check("process_exit_zero", session.get("exit_code") == 0, {"exit_code": session.get("exit_code")})
    files = {}
    for name, present, absent in expected:
        path = RESULTS / name
        found = paragraphs(path) if path.is_file() else []
        files[name] = {"sha256": sha256(path) if path.is_file() else None, "paragraphs": found}
        check(f"{name}_paragraphs", found == present and not any(a in "".join(found) for a in absent),
              {"expected": present, "found": found})
    source_path, fixture = RESULTS / source_name, FIXTURE
    source_paragraphs = paragraphs(source_path)
    files[source_name] = {"sha256": sha256(source_path), "fixture_sha256": sha256(fixture),
                          "paragraphs": source_paragraphs}
    if args.same_path_marker:
        # Saved to its own path once, then the later edit was discarded on replacement.
        original = paragraphs(fixture)
        remaining = [text for text in source_paragraphs if text != args.same_path_marker]
        check("opened_document_saved_in_place_then_discard_kept_out",
              sha256(source_path) != sha256(fixture) and source_paragraphs.count(args.same_path_marker) == 1
              and remaining == original and DISCARDED not in "".join(source_paragraphs),
              {"marker": args.same_path_marker, "original_paragraphs": original, "found": source_paragraphs})
    else:
        check("replaced_document_unchanged_after_discard",
              sha256(source_path) == sha256(fixture) and DISCARDED not in "".join(source_paragraphs),
              {"sha256": sha256(source_path), "fixture_sha256": sha256(fixture)})

    created = [int(view) for action, _, view in actions if action == "host_created"]
    retired = [int(view) for action, _, view in actions if action == "view_retired"]
    destroyed = [int(view) for action, _, view in actions if action == "retired_view_destroyed"]
    check("replacement_views_retired_and_destroyed", len(retired) == 2 and sorted(retired) == sorted(destroyed),
          {"views_created": created, "retired": retired, "destroyed": destroyed})
    check("editor_initiated_save_dialog_answered",
          sum(1 for action, _, _ in actions if action == "save_destination_requested") == 2
          and sum(1 for action, _, _ in actions if action == "save_destination_cancelled") == 1,
          {"requested": sum(1 for a, _, _ in actions if a == "save_destination_requested"),
           "cancelled": sum(1 for a, _, _ in actions if a == "save_destination_cancelled")})
    failures = [a for a, _, _ in actions if a in ("operation_failed", "operation_timeout")]
    check("no_failed_or_timed_out_operation", not failures, {"failed": failures})

    report = {
        "checked_at_utc": datetime.now(timezone.utc).isoformat(),
        "session": args.label,
        "runtime": session.get("runtime"),
        "entry_sha256": session.get("entry_sha256"),
        "native_events_sha256": session.get("native_events_sha256"),
        "prefix": args.prefix,
        "same_path_marker": args.same_path_marker,
        "close_discarded": args.close_discarded,
        "steps_performed_via_desktop_control": [
            "新建文档 -> 文档1.docx，输入第一段",
            f"顶部保存 -> 系统保存对话框 -> {args.prefix}-new.docx",
            f"输入第二段 -> 另存为 -> {args.prefix}-copy.docx",
            f"开始 -> 选择本地文件 {source_name} -> 打开所选文件（当前文档已保存，无提示）",
        ] + ([f"新起一段输入“{args.same_path_marker}” -> 顶部保存（原路径）"] if args.same_path_marker else []) + [
            "在该文档里输入“放弃的修改” -> 新建文档 -> 提示选择不保存 -> 文档2.docx",
            f"文档2 输入文字 -> 编辑器内 Ctrl+S -> 对话框取消 -> 再次 Ctrl+S -> {args.prefix}-editor.docx",
            "未修改时点顶部保存 -> 立即提示无需再次写入",
        ] + ([f"输入“{args.close_discarded}” -> 窗口关闭按钮 -> 提示选择不保存 -> 退出"] if args.close_discarded
             else ["窗口关闭按钮退出"]),
        "files": files,
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
