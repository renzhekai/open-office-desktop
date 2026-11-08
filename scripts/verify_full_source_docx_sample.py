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
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--after", type=Path, default=AFTER, help="saved fixture copy (default: the accepted run's)")
    parser.add_argument("--marker", default=MARKER, help="marker paragraph typed in the GUI")
    args = parser.parse_args()
    after_path = args.after if args.after.is_absolute() else ROOT / args.after
    marker = args.marker
    before, after = snapshot(BEFORE), snapshot(after_path)
    checks = {
        "docx_package_valid": before["kind"] == after["kind"] == "docx",
        "exactly_one_added_marker_paragraph": after["paragraphs"] == [marker] + before["paragraphs"],
        "original_table_values_and_structure_preserved": after["tables"] == before["tables"],
        "saved_bytes_changed": BEFORE.read_bytes() != after_path.read_bytes(),
    }
    report = {"passed": all(checks.values()), "checks": checks,
              "marker": marker,
              "original": {"path": str(BEFORE), "sha256": hashlib.sha256(BEFORE.read_bytes()).hexdigest()},
              "saved": {"path": str(after_path), "sha256": hashlib.sha256(after_path.read_bytes()).hexdigest(),
                        "bytes": after_path.stat().st_size},
              "saved_semantics": after,
              "scope": "Fixture text and table semantics only; source provenance, GUI input and reopening require separate evidence."}
    print(json.dumps(report, ensure_ascii=True, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
