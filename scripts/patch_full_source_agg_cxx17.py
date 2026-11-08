"""Remove obsolete C++ register specifiers in isolated AGG headers only."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "build/full-source/DesktopEditors/core/DesktopEditor/agg-2.4/include"
REPLACEMENTS = {
    "agg_basics.h": [(b"register unsigned q", b"unsigned q", 1)],
    "agg_image_accessors.h": [(b"register int x", b"int x", 1),
                              (b"register int y", b"int y", 1)],
    "agg_trans_affine.h": [(b"register double tmp", b"double tmp", 2),
                           (b"register double d", b"double d", 1),
                           (b"register double a", b"double a", 1),
                           (b"register double b", b"double b", 1)],
}
records = []
for name, replacements in REPLACEMENTS.items():
    path = BASE / name
    data = path.read_bytes()
    original = data
    for old, new, count in replacements:
        found = data.count(old)
        if found not in (0, count) or (found == 0 and data.count(new) < count):
            raise RuntimeError(f"Unexpected isolated header: {path}, {old!r}")
        data = data.replace(old, new)
    if data != original:
        path.write_bytes(data)
    records.append({"path": str(path.relative_to(ROOT)),
                    "changed_this_run": data != original,
                    "sha256": hashlib.sha256(data).hexdigest()})
output = ROOT / "evidence/full-source-agg-cxx17-patch.json"
output.write_text(json.dumps({"reason": "C++17 removed register storage specifier",
                              "headers": records}, indent=2) + "\n", encoding="utf-8")
print("AGG_CXX17_HEADERS_READY", len(records))
