"""Check the saved GUI-test DOCX against its original fixture.

This verifies the small fixture's text/table semantics, not rendering or general
Office compatibility. Running this script does not operate the editing engine.
"""
import hashlib
import json
from pathlib import Path
from roundtrip import snapshot


ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / "fixtures/generated/text-table.docx"
AFTER = ROOT / "fixtures/results/native-docx-roundtrip.docx"
MARKER = "原生引擎编辑保存验证 2026-11-01"


def main():
    before = snapshot(BEFORE)
    after = snapshot(AFTER)
    checks = {
        "docx_package_valid": before["kind"] == after["kind"] == "docx",
        "exactly_one_added_marker_paragraph": after["paragraphs"] == [MARKER] + before["paragraphs"],
        "original_table_values_and_structure_preserved": after["tables"] == before["tables"],
        "saved_bytes_changed": BEFORE.read_bytes() != AFTER.read_bytes(),
    }
    report = {
        "passed": all(checks.values()),
        "checks": checks,
        "original": {"path": str(BEFORE), "sha256": hashlib.sha256(BEFORE.read_bytes()).hexdigest()},
        "saved": {"path": str(AFTER), "sha256": hashlib.sha256(AFTER.read_bytes()).hexdigest(), "bytes": AFTER.stat().st_size},
        "saved_semantics": after,
        "scope": "Fixture text and table contents only; GUI opening/editing/reopening is separate evidence.",
    }
    print(json.dumps(report, ensure_ascii=True, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
