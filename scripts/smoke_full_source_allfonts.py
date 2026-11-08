"""Exercise the source-built graphics/font stack on bundled public fonts."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "build/full-source/DesktopEditors/core"
OUT = ROOT / "build/full-source/core-evidence/allfonts-smoke"
FONTS = ROOT / "dependencies/onlyoffice-v10.0.0/runtime/fonts"


def record(path):
    return {"path": str(path.relative_to(ROOT)), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    exe = CORE / "build/bin/win_64/allfontsgen.exe"
    command = [str(exe), "--use-system=false", "--use-system-user-fonts=false",
               "--input=" + str(FONTS), "--allfonts=" + str(OUT / "AllFonts.js"),
               "--selection=" + str(OUT / "font_selection.bin")]
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join([str(CORE / "build/lib/win_64"),
                                   str(CORE / "Common/3dParty/icu/win_64/build"),
                                   str(Path(os.environ["SystemRoot"]) / "System32")])
    with (OUT / "run.log").open("wb") as log:
        process = subprocess.run(command, cwd=OUT, env=env, stdout=log,
                                 stderr=subprocess.STDOUT, timeout=120)
    javascript = OUT / "AllFonts.js"
    selection = OUT / "font_selection.bin"
    checks = {"exit_zero": process.returncode == 0,
              "font_index_generated": selection.is_file() and selection.stat().st_size > 100,
              "known_bundled_family_found": javascript.is_file() and
              "Open Sans" in javascript.read_text(encoding="utf-8-sig")}
    result = {"checked_at_utc": datetime.now(timezone.utc).isoformat(),
              "passed": all(checks.values()), "exit_code": process.returncode,
              "checks": checks, "command": command,
              "source_built_executable": record(exe),
              "source_built_dlls": [record(CORE / "build/lib/win_64" / name)
                                    for name in ("graphics.dll", "kernel.dll", "UnicodeConverter.dll")],
              "input_fonts": [record(p) for p in sorted(FONTS.glob("*.ttf"))],
              "outputs": [record(p) for p in (javascript, selection) if p.is_file()],
              "scope": "Font parsing/index generation through source-built graphics; no DOCX GUI claim."}
    (OUT / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "checks": checks, "exit_code": process.returncode}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
