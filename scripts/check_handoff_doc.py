#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check the maintained AI handoff document.

Verifies that appendix A of Open_Office_AI接手文档.md is byte-identical to
requirements.md, that the required maintenance sections exist, and prints the
document SHA256 so a change-log entry can cite it. Read-only.

Usage:
    python -I scripts/check_handoff_doc.py
"""

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "Open_Office_AI接手文档.md"
REQUIREMENTS = ROOT / "requirements.md"
BEGIN = "<!-- BEGIN REQUIREMENTS ORIGINAL BYTES -->\n````markdown\n".encode("utf-8")
END = "````\n<!-- END REQUIREMENTS ORIGINAL BYTES -->".encode("utf-8")
REQUIRED_HEADINGS = ["## 维护规则", "## 0. 当前速览", "### 2.4 用户在对话中的明确指令", "## 13. 变更日志",
                     "## 附录 A：requirements.md 完整原文"]


def main():
    doc = DOC.read_bytes()
    requirements = REQUIREMENTS.read_bytes()
    problems = []
    if doc.count(BEGIN) != 1 or doc.count(END) != 1:
        problems.append("appendix markers missing or duplicated")
    else:
        start = doc.index(BEGIN) + len(BEGIN)
        appendix = doc[start:doc.index(END)]
        if appendix != requirements:
            problems.append(f"appendix differs from requirements.md ({len(appendix)} vs {len(requirements)} bytes)")
    text = doc.decode("utf-8")
    for heading in REQUIRED_HEADINGS:
        if heading not in text:
            problems.append(f"missing heading: {heading}")
    print(f"document sha256 {hashlib.sha256(doc).hexdigest()} bytes {len(doc)}")
    print(f"requirements sha256 {hashlib.sha256(requirements).hexdigest()}")
    if problems:
        print("FAILED: " + "; ".join(problems))
        sys.exit(1)
    print("OK: appendix A byte-identical to requirements.md; maintenance sections present")


if __name__ == "__main__":
    main()
