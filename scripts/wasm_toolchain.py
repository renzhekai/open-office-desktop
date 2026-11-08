#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Project-local Emscripten 3.1.48 environment and toolchain smoke test.

The toolchain lives in dependencies/emscripten-3.1.48 (official wasm-binaries.zip,
SHA256-verified). Configuration and cache live in build/wasm-source/toolchain.
Only the child process environment is changed: no system PATH, registry or
global emsdk activation. Python is the already approved project-local 3.11.8;
Node is the existing system Node.

Usage:
    python -I scripts/wasm_toolchain.py smoke --label <new-label>
"""

import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EMSDK_INSTALL = ROOT / "dependencies" / "emscripten-3.1.48" / "install" / "install"
EMSCRIPTEN = EMSDK_INSTALL / "emscripten"
LLVM_BIN = EMSDK_INSTALL / "bin"
PYTHON = ROOT / "dependencies" / "v8-tools" / "python-3.11.8" / "python.exe"
TOOLCHAIN_DIR = ROOT / "build" / "wasm-source" / "toolchain"
CONFIG = TOOLCHAIN_DIR / "emscripten_config"
CACHE = TOOLCHAIN_DIR / "cache"
CLOSURE_NATIVE = EMSCRIPTEN / "node_modules" / "google-closure-compiler-windows" / "compiler.exe"
EXPECTED_VERSION = "3.1.48"


def node_path():
    found = shutil.which("node")
    if not found:
        sys.exit("node not found")
    return Path(found).resolve()


def config_text():
    return (
        "# Project-local Emscripten config written by scripts/wasm_toolchain.py\n"
        f"LLVM_ROOT = r'{LLVM_BIN}'\n"
        f"BINARYEN_ROOT = r'{EMSDK_INSTALL}'\n"
        f"NODE_JS = r'{node_path()}'\n"
        "JS_ENGINES = [NODE_JS]\n"
    )


def ensure_config():
    text = config_text()
    if CONFIG.exists():
        if CONFIG.read_text(encoding="utf-8") != text:
            sys.exit(f"{CONFIG} differs from the expected content; refusing to overwrite")
        return
    TOOLCHAIN_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(text, encoding="utf-8")


def toolchain_env():
    ensure_config()
    env = dict(os.environ)
    env["EM_CONFIG"] = str(CONFIG)
    env["EM_CACHE"] = str(CACHE)
    env["EMSDK_PYTHON"] = str(PYTHON)
    env["PYTHONUTF8"] = "1"
    return env


def emcc(cpp=False):
    return [str(PYTHON), str(EMSCRIPTEN / ("em++.py" if cpp else "emcc.py"))]


def run(cmd, cwd, env, log):
    result = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    log.append({"cmd": [str(c) for c in cmd], "cwd": str(cwd), "exit": result.returncode,
                "stdout": result.stdout[-4000:], "stderr": result.stderr[-4000:]})
    return result


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


HELLO_C = r"""#include <stdio.h>
int add(int a, int b) { return a + b; }
int main(void) { printf("hello wasm %d\n", add(40, 2)); return 0; }
"""

EXCEPTIONS_CPP = r"""#include <cstdio>
#include <stdexcept>
#include <string>
static int thrower(int v) { if (v > 5) throw std::runtime_error(std::to_string(v)); return v; }
int main() {
    try { thrower(7); } catch (const std::exception& e) { std::printf("caught %s\n", e.what()); return 0; }
    return 1;
}
"""


def smoke(label):
    work = TOOLCHAIN_DIR / f"smoke-{label}"
    evidence = ROOT / "evidence" / "wasm-source" / f"toolchain-smoke-{label}.json"
    if work.exists() or evidence.exists():
        sys.exit(f"label {label} already used")
    work.mkdir(parents=True)
    (work / "hello.c").write_text(HELLO_C, encoding="utf-8")
    (work / "exceptions.cpp").write_text(EXCEPTIONS_CPP, encoding="utf-8")
    env = toolchain_env()
    node = str(node_path())
    log = []
    checks = {}

    version = run(emcc() + ["--version"], work, env, log)
    checks["emcc_version"] = version.returncode == 0 and EXPECTED_VERSION in version.stdout

    steps = [
        ("wasm_c", emcc() + ["-O2", "-s", "WASM=1", "hello.c", "-o", "hello.js"], "hello.js", "hello wasm 42"),
        ("wasm_cpp_exceptions", emcc(True) + ["-O2", "-fexceptions", "-s", "WASM=1", "exceptions.cpp", "-o", "exc.js"],
         "exc.js", "caught 7"),
        ("asmjs_wasm2js", emcc() + ["-O2", "-s", "WASM=0", "hello.c", "-o", "hello_asm.js"], "hello_asm.js", "hello wasm 42"),
    ]
    outputs = {}
    for name, cmd, js, expected in steps:
        built = run(cmd, work, env, log)
        ran = run([node, js], work, env, log) if built.returncode == 0 else None
        checks[name] = bool(ran and ran.returncode == 0 and expected in ran.stdout)
        for produced in sorted(work.glob(Path(js).stem + ".*")):
            outputs[produced.name] = {"bytes": produced.stat().st_size, "sha256": sha256(produced)}

    closure = run([str(CLOSURE_NATIVE), "--version"], work, env, log)
    checks["closure_native"] = closure.returncode == 0 and "Version" in closure.stdout

    node_version = run([node, "--version"], work, env, log).stdout.strip()
    python_version = run([str(PYTHON), "--version"], work, env, log).stdout.strip()
    report = {
        "label": label,
        "checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "emscripten_install": str(EMSDK_INSTALL),
        "em_config": str(CONFIG), "em_config_text": config_text(), "em_cache": str(CACHE),
        "python": {"path": str(PYTHON), "version": python_version},
        "node": {"path": node, "version": node_version},
        "system_path_changed": False,
        "checks": checks,
        "all_passed": all(checks.values()),
        "outputs": outputs,
        "log": log,
    }
    evidence.parent.mkdir(parents=True, exist_ok=True)
    with open(evidence, "x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"checks": checks, "evidence": str(evidence)}, ensure_ascii=False))
    return 0 if report["all_passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    smoke_parser = sub.add_parser("smoke")
    smoke_parser.add_argument("--label", required=True)
    args = parser.parse_args()
    if args.command == "smoke":
        sys.exit(smoke(args.label))


if __name__ == "__main__":
    main()
