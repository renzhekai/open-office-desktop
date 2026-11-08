"""Run the corrected RGB HEIF test without overwriting earlier diagnostics."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import full_source_graphics_heif_build as build

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("label")
args = parser.parse_args()
if not re.fullmatch(r"[a-z0-9-]+", args.label):
    parser.error("Use a lowercase alphanumeric/hyphen label")
if build.ENV.get("VCTOOLSVERSION", "").rstrip("\\/") != "14.44.35207":
    raise RuntimeError("Use the supplied vcvars wrapper")
build.EVIDENCE = build.AREA / "graphics-dependency-evidence" / args.label
build.EVIDENCE.mkdir(exist_ok=False)
libraries = [build.build_dir("x265") / "Release/x265-static.lib",
             build.build_dir("libde265") / "libde265/Release/libde265.lib",
             build.build_dir("libheif") / "libheif/Release/heif.lib"]
before = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
          for path in libraries}
build.smoke()
after = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
         for path in libraries}
assert before == after, "Test must not modify production libraries"
result = {"production_libraries_unchanged": True, "libraries": after,
          "source": "libheif/examples/heif_enc.cc lossless RGB configuration",
          "original_ycbcr_limitation": "libheif 1.18.2 decodes through RGB before returning YCbCr420; sample identity is not guaranteed",
          "result": json.loads((build.EVIDENCE / "heif-smoke-result.json").read_text())}
(build.EVIDENCE / "validation-result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result, indent=2), flush=True)
