"""Verify the saved source-core GUI fixture; GUI evidence is separate."""
import hashlib
import json
from pathlib import Path
from roundtrip import snapshot

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / "fixtures/generated/text-table.docx"
AFTER = ROOT / "fixtures/results/full-source-docx-roundtrip.docx"
MARKER = "自产原生核心编辑保存验证 2026-11-02"


def main():
    before, after = snapshot(BEFORE), snapshot(AFTER)
    checks = {
        "docx_package_valid": before["kind"] == after["kind"] == "docx",
        "exactly_one_added_marker_paragraph": after["paragraphs"] == [MARKER] + before["paragraphs"],
        "original_table_values_and_structure_preserved": after["tables"] == before["tables"],
        "saved_bytes_changed": BEFORE.read_bytes() != AFTER.read_bytes(),
    }
    report = {"passed": all(checks.values()), "checks": checks,
              "marker": MARKER,
              "original": {"path": str(BEFORE), "sha256": hashlib.sha256(BEFORE.read_bytes()).hexdigest()},
              "saved": {"path": str(AFTER), "sha256": hashlib.sha256(AFTER.read_bytes()).hexdigest(),
                        "bytes": AFTER.stat().st_size},
              "saved_semantics": after,
              "scope": "Fixture text and table semantics only; source provenance, GUI input and reopening require separate evidence."}
    print(json.dumps(report, ensure_ascii=True, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
