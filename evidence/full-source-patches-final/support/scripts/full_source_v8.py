"""Controlled V8 acquisition/build; never execute unreviewed hooks or bootstrap.

All sources/tools/caches are confined to the existing isolated V8 checkout and
project-owned directories. No system/user Git configuration is changed.
"""
import argparse
import ast
import base64
import concurrent.futures
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import urllib.parse
import zipfile

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/full-source"
V8 = WORK / "DesktopEditors/core/Common/3dParty/v8_89/v8"
TOOLS = ROOT / "dependencies/v8-tools"
EVIDENCE = WORK / "v8-evidence"
PIN = "a2d8eca22ad478a95ca67bb2421a9a2535113925"
GIT = ["git", "-c", "core.hooksPath=" + str(WORK / "empty-hooks"), "-c", "core.longpaths=true", "-c", "core.autocrlf=false", "-c", "http.version=HTTP/1.1", "-c", "credential.helper="]
ENV = os.environ.copy()
ENV.update(GIT_TERMINAL_PROMPT="0", GIT_OPTIONAL_LOCKS="0", DEPOT_TOOLS_UPDATE="0", DEPOT_TOOLS_WIN_TOOLCHAIN="0", PYTHONNOUSERSITE="1", PYTHONDONTWRITEBYTECODE="1", CIPD_CACHE_DIR=str(TOOLS / "cache/cipd"), VPYTHON_VIRTUALENV_ROOT=str(TOOLS / "cache/vpython"), XDG_CACHE_HOME=str(TOOLS / "cache"))


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def run(argv, name, cwd=None, timeout=600, check=True):
    with (EVIDENCE / (name + ".log")).open("a", encoding="utf-8") as log:
        log.write(json.dumps({"at": timestamp(), "argv": [str(a) for a in argv], "cwd": str(cwd)}) + "\n")
        log.flush()
        result = subprocess.run(argv, cwd=cwd, env=ENV, capture_output=True, timeout=timeout)
        log.write(result.stdout.decode("utf-8", "replace") + result.stderr.decode("utf-8", "replace") + "\nexit=" + str(result.returncode) + "\n")
    if check and result.returncode:
        raise RuntimeError(name + ": " + result.stderr.decode("utf-8", "replace")[-2500:])
    return result


def literal(node, variables):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Dict):
        return {literal(k, variables): literal(v, variables) for k, v in zip(node.keys, node.values)}
    if isinstance(node, (ast.List, ast.Tuple)):
        return [literal(v, variables) for v in node.elts]
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return literal(node.left, variables) + literal(node.right, variables)
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "Var" and len(node.args) == 1 and not node.keywords:
        return variables[literal(node.args[0], variables)]
    raise ValueError("Unsupported DEPS syntax: " + ast.dump(node))


def deps():
    values = {}
    for node in ast.parse((V8 / "DEPS").read_text(encoding="utf-8")).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in ("vars", "deps", "hooks", "gclient_gn_args"):
                values[name] = literal(node.value, values.get("vars", {}))
    return values


def acquire_repo(relative, specification):
    url, commit = specification.rsplit("@", 1)
    if not url.startswith(("https://chromium.googlesource.com/", "https://android.googlesource.com/")) or len(commit) != 40:
        raise ValueError("Unexpected source origin or revision")
    destination = V8 / relative
    if not destination.resolve().is_relative_to(V8.resolve()):
        raise ValueError("Source destination escapes V8")
    name = "fetch-" + relative.replace("/", "_")
    if not (destination / ".git").is_dir():
        if destination.exists() and any(destination.iterdir()):
            raise ValueError("Unowned nonempty destination: " + str(destination))
        destination.mkdir(parents=True, exist_ok=True)
        run(GIT + ["init", "--initial-branch=source-build", str(destination)], name)
        run(GIT + ["remote", "add", "origin", url], name, destination)
    actual = run(GIT + ["remote", "get-url", "origin"], name, destination).stdout.decode().strip()
    if actual != url:
        raise ValueError("Existing repository URL mismatch: " + relative)
    head = run(GIT + ["rev-parse", "--verify", "HEAD"], name, destination, check=False)
    if head.returncode:
        run(GIT + ["fetch", "--depth=1", "origin", commit], name, destination)
        run(GIT + ["checkout", "--detach", commit], name, destination)
    elif head.stdout.decode().strip() != commit:
        raise ValueError("Existing repository revision mismatch: " + relative)
    actual_commit = run(GIT + ["rev-parse", "HEAD"], name, destination).stdout.decode().strip()
    if actual_commit != commit:
        raise ValueError("Fetched source differs from DEPS")
    print(relative + " " + actual_commit, flush=True)
    return {"path": relative, "url": url, "commit": commit, "status": "checked_out", "at": timestamp()}


def sync(group):
    if run(GIT + ["rev-parse", "HEAD"], "source-pin", V8).stdout.decode().strip() != PIN:
        raise ValueError("V8 source pin mismatch")
    data = deps()
    # Windows monolith source dependencies. Test corpora, distributed test
    # services, other-platform sources and unused formatting tools are excluded.
    selected = {k: v for k, v in data["deps"].items() if isinstance(v, str) and not k.startswith(("test/", "tools/swarming", "buildtools/clang_format", "third_party/instrumented_libraries"))}
    bootstrap = {"build", "buildtools", "third_party/depot_tools", "third_party/jinja2", "third_party/markupsafe"}
    if group == "bootstrap":
        selected = {k: v for k, v in selected.items() if k in bootstrap}
    elif group == "remaining":
        selected = {k: v for k, v in selected.items() if k not in bootstrap}
    manifest_path = EVIDENCE / "sources.json"
    records = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    save(EVIDENCE / "deps-audit.json", {"v8_commit": PIN, "deps_sha256": digest(V8 / "DEPS"), "parsed_dependencies": data["deps"], "hooks": [{"name": h["name"], "action": h.get("action"), "condition": h.get("condition"), "status": "not_executed"} for h in data["hooks"]], "strategy": "Explicit pinned Git sources, no hooks/bootstrap; GN is the only DEPS CIPD binary needed for this target."})
    failures = []
    # Parent directories must be checked out before nested repositories.
    for depth in sorted({k.count("/") for k in selected}):
        batch = {k: v for k, v in selected.items() if k.count("/") == depth}
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            pending = {pool.submit(acquire_repo, k, v): k for k, v in batch.items()}
            for future in concurrent.futures.as_completed(pending):
                name = pending[future]
                try:
                    records[name] = future.result()
                except Exception as error:
                    records[name] = {"status": "failed", "error": str(error)}
                    failures.append(name)
                    print(name + " FAILED " + str(error), flush=True)
                save(manifest_path, records)
    if failures:
        raise RuntimeError("Source acquisition failed: " + ", ".join(failures))


def download(url, filename, expected_sha256=None):
    destination = TOOLS / filename
    part = destination.with_name(destination.name + ".part")
    if destination.exists():
        if expected_sha256 and digest(destination) != expected_sha256:
            raise ValueError("Existing download hash mismatch")
        return destination
    run(["curl.exe", "--disable", "--fail", "--location", "--http1.1", "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "20", "--max-time", "600", "--retry", "2", "--continue-at", "-", "--silent", "--show-error", "--output", str(part), url], "download-" + filename.replace("/", "_"), timeout=700)
    if expected_sha256 and digest(part) != expected_sha256:
        raise ValueError("Downloaded file hash mismatch: " + filename)
    part.rename(destination)
    return destination


def python_metadata():
    url = "https://www.python.org/ftp/python/3.11.8/python-3.11.8-embed-amd64.zip.sigstore"
    path = download(url, "python-3.11.8-embed-amd64.zip.sigstore")
    data = json.loads(path.read_text())
    print(json.dumps({"source": url, "messageDigest": data["messageSignature"]["messageDigest"]}), flush=True)


def tools():
    metadata = TOOLS / "python-3.11.8-embed-amd64.zip.sigstore"
    if not metadata.exists():
        python_metadata()
    bundle = json.loads(metadata.read_text())
    message = bundle["messageSignature"]["messageDigest"]
    if message["algorithm"] != "SHA2_256":
        raise ValueError("Unexpected Python digest algorithm")
    expected = base64.b64decode(message["digest"]).hex()
    url = "https://www.python.org/ftp/python/3.11.8/python-3.11.8-embed-amd64.zip"
    archive = download(url, "python-3.11.8-embed-amd64.zip", expected)
    target = WORK / "v8-tools/python-3.11.8"
    target.mkdir(parents=True, exist_ok=True)
    entries = []
    with zipfile.ZipFile(archive) as source:
        for info in source.infolist():
            relative = PurePosixPath(info.filename)
            if relative.is_absolute() or ".." in relative.parts or "\\" in info.filename or ":" in info.filename or ((info.external_attr >> 16) & 0o170000) == 0o120000:
                raise ValueError("Unsafe Python archive member")
            if info.is_dir():
                continue
            destination = target.joinpath(*relative.parts)
            contents = source.read(info)
            # Preserve a previously recorded local initialization adjustment.
            if info.filename == "python311._pth" and (target / "python311._pth.original").exists():
                if (target / "python311._pth.original").read_bytes() != contents:
                    raise ValueError("Original embedded Python configuration differs")
                continue
            if destination.exists() and destination.read_bytes() != contents:
                raise ValueError("Existing Python tool file differs from official archive")
            destination.parent.mkdir(parents=True, exist_ok=True)
            if not destination.exists():
                destination.write_bytes(contents)
            entries.append({"path": info.filename, "size": len(contents), "sha256": hashlib.sha256(contents).hexdigest()})
    # Use normal CPython script-directory imports while suppressing user site
    # via the process environment. Keep the official embedded config verbatim.
    pth = target / "python311._pth"
    if pth.exists():
        pth.rename(target / "python311._pth.original")
    py_result = run([str(target / "python.exe"), "-s", "-c", "import sys,ssl,json; print(json.dumps({'version':sys.version,'executable':sys.executable,'path':sys.path}))"], "python-version")
    print(py_result.stdout.decode(), flush=True)
    depot = V8 / "third_party/depot_tools"
    version = (depot / "cipd_client_version").read_text().strip()
    checksums = [line.split() for line in (depot / "cipd_client_version.digests").read_text().splitlines() if line.startswith("windows-amd64")]
    if len(checksums) != 1 or checksums[0][1] != "sha256":
        raise ValueError("Pinned CIPD client digest missing")
    cipd_url = "https://chrome-infra-packages.appspot.com/client?" + urllib.parse.urlencode({"platform": "windows-amd64", "version": version})
    cipd = download(cipd_url, "cipd.exe", checksums[0][2])
    save(EVIDENCE / "tools.json", {"python": {"url": url, "sha256": digest(archive), "expected_sha256": expected, "checksum_source": str(metadata), "size": archive.stat().st_size, "deployment": str(target), "signature_verification": "Digest compared with official HTTPS-served Sigstore metadata; signature chain not cryptographically verified", "local_configuration": "python311._pth renamed to .original for normal script imports; PYTHONNOUSERSITE=1 process-only", "members": entries}, "cipd": {"url": cipd_url, "version": version, "sha256": digest(cipd), "size": cipd.stat().st_size, "checksum_source": str(depot / "cipd_client_version.digests")}, "python2_installed": False})
    print(run([str(cipd), "help", "ensure"], "cipd-help").stdout.decode("utf-8", "replace")[:15000], flush=True)


def gn():
    cipd = TOOLS / "cipd.exe"
    version = deps()["vars"]["gn_version"]
    ensure = EVIDENCE / "gn.ensure"
    ensure.write_text("gn/gn/windows-amd64 " + version + "\n", encoding="utf-8")
    root = V8 / "buildtools/win"
    run([str(cipd), "ensure", "-root", str(root), "-ensure-file", str(ensure), "-cache-dir", str(TOOLS / "cache/cipd"), "-json-output", str(EVIDENCE / "gn-cipd.json")], "gn-cipd-ensure")
    exe = root / "gn.exe"
    version_result = run([str(exe), "--version"], "gn-version").stdout.decode().strip()
    save(EVIDENCE / "gn.json", {"package": "gn/gn/windows-amd64", "version": version, "version_output": version_result, "path": str(exe), "size": exe.stat().st_size, "sha256": digest(exe), "cipd_receipt": str(EVIDENCE / "gn-cipd.json")})
    print("GN ready " + version_result, flush=True)


def patch(relative, old, new, label):
    path = V8 / relative
    contents = path.read_text(encoding="utf-8")
    receipt = EVIDENCE / "patches.json"
    records = json.loads(receipt.read_text()) if receipt.exists() else []
    if new in contents:
        return
    if contents.count(old) != 1:
        raise ValueError("Patch anchor not unique: " + relative + " / " + label)
    before = digest(path)
    path.write_text(contents.replace(old, new, 1), encoding="utf-8", newline="\n")
    records.append({"path": relative, "label": label, "before_sha256": before, "after_sha256": digest(path), "at": timestamp()})
    save(receipt, records)


def prepare():
    # Repair duplicate insertions from an interrupted earlier preparation;
    # only exact adjacent blocks produced by this script can be collapsed.
    repeated = {
        "BUILD.gn": ['  if (is_win) {\n    sources += [ "src/base/platform/wrappers.cc" ]\n  }\n'],
        "build/vs_toolchain.py": ["  ('2026', '18.0'),\n", "   '2026' : 'VC143',  # Explicitly using installed MSVC 14.44\n", "  if component == 'Tools' and os.environ.get('OO_V8_MSVC_TOOLS'):\n    pinned_tools = os.path.join(vc_component_msvc_root, os.environ['OO_V8_MSVC_TOOLS'])\n    if not os.path.isdir(pinned_tools):\n      raise RuntimeError('Pinned MSVC tools directory missing')\n    return pinned_tools\n"],
        "build/toolchain/win/tool_wrapper.py": ["      line = line.decode('utf-8', errors='replace')\n"],
    }
    for relative, blocks in repeated.items():
        path = V8 / relative
        content = path.read_text(encoding="utf-8")
        revised = content
        for block in blocks:
            while block + block in revised:
                revised = revised.replace(block + block, block)
        if revised != content:
            before = digest(path)
            path.write_text(revised, encoding="utf-8", newline="\n")
            receipt = EVIDENCE / "patches.json"
            rows = json.loads(receipt.read_text()) if receipt.exists() else []
            rows.append({"path": relative, "label": "Collapse exact duplicate owned preparation blocks", "before_sha256": before, "after_sha256": digest(path), "at": timestamp()})
            save(receipt, rows)
    patch("build/config/win/BUILD.gn", 'configs = [ ":static_crt" ]', 'configs = [ ":dynamic_crt" ]  # ONLYOFFICE /MD ABI', "ONLYOFFICE dynamic CRT")
    patch("build/vs_toolchain.py", "  ('2019', '16.0'),", "  ('2026', '18.0'),\n  ('2019', '16.0'),", "Recognize existing VS2026 installation")
    patch("build/vs_toolchain.py", "   '2019' : 'VC142',", "   '2026' : 'VC143',  # Explicitly using installed MSVC 14.44\n   '2019' : 'VC142',", "Selected existing v143 toolset")
    patch("build/vs_toolchain.py", "debug_files.extend([('api-ms-win-downlevel-kernel32-l2-1-0.dll', False),\n                        ('api-ms-win-eventing-provider-l1-1-0.dll', False)])", "debug_files.extend([('api-ms-win-downlevel-kernel32-l2-1-0.dll', sys.getwindowsversion().major >= 10),\n                        ('api-ms-win-eventing-provider-l1-1-0.dll', sys.getwindowsversion().major >= 10)])", "Windows10+ API-set contracts resolve without separate legacy shim files; SDK dbghelp actual LoadLibrary verified")
    patch("build/vs_toolchain.py", "  vc_component_msvc_contents = os.listdir(vc_component_msvc_root)", "  if component == 'Tools' and os.environ.get('OO_V8_MSVC_TOOLS'):\n    pinned_tools = os.path.join(vc_component_msvc_root, os.environ['OO_V8_MSVC_TOOLS'])\n    if not os.path.isdir(pinned_tools):\n      raise RuntimeError('Pinned MSVC tools directory missing')\n    return pinned_tools\n  vc_component_msvc_contents = os.listdir(vc_component_msvc_root)", "Prevent selecting installed newer 14.51")
    patch("build/toolchain/win/setup_toolchain.py", "    args.append('10.0.19041.0')", "    args.append(os.environ['OO_V8_SDK_VERSION'])\n    args.append('-vcvars_ver=' + os.environ['OO_V8_MSVC_TOOLS'])", "Explicit existing SDK and MSVC selection")
    patch("build/toolchain/win/tool_wrapper.py", "    for line in link.stdout:\n", "    for line in link.stdout:\n      line = line.decode('utf-8', errors='replace')\n", "ONLYOFFICE Python3 link output decoding")
    patch("third_party/jinja2/tests.py", "from collections import Mapping", "from collections.abc import Mapping", "ONLYOFFICE Python3.11 Jinja compatibility")
    patch("BUILD.gn", '  sources = [ "src/base/platform/wrappers.h" ]', '  sources = [ "src/base/platform/wrappers.h" ]\n  if (is_win) {\n    sources += [ "src/base/platform/wrappers.cc" ]\n  }', "Express ONLYOFFICE nonempty wrappers object in GN instead of editing generated Ninja")
    wrapper = V8 / "src/base/platform/wrappers.cc"
    wrapper_contents = '#include "src/base/platform/wrappers.h"\n'
    if wrapper.exists() and wrapper.read_text() != wrapper_contents:
        raise ValueError("Unexpected existing wrappers.cc")
    wrapper.write_text(wrapper_contents, encoding="utf-8", newline="\n")
    generated = V8 / "build/config/gclient_args.gni"
    generated.write_text("checkout_fuchsia_for_arm64_host = false\ncheckout_google_benchmark = false\n", encoding="utf-8", newline="\n")
    output = V8 / "out.gn/win_64/release"
    output.mkdir(parents=True, exist_ok=True)
    arguments = {"is_debug": False, "is_component_build": False, "is_clang": False, "use_lld": False,
                 "target_cpu": "x64", "v8_target_cpu": "x64", "v8_static_library": True,
                 "v8_monolithic": True, "v8_use_external_startup_data": False, "v8_enable_pointer_compression": True,
                 "use_custom_libcxx": False, "treat_warnings_as_errors": False, "symbol_level": 1,
                 "use_goma": False, "enable_iterator_debugging": False}
    (output / "args.gn").write_text("\n".join(k + " = " + json.dumps(v) for k, v in arguments.items()) + "\n", encoding="utf-8")
    print("V8 patches and Release args prepared", flush=True)


def build_environment():
    alias = Path("C:/Users/18268/AppData/Local/Temp/oo10-full-src")
    tools_alias = alias / "v8-tools"
    if not tools_alias.exists() or tools_alias.resolve() != (WORK / "v8-tools").resolve():
        raise ValueError("Expected project-owned runtime directory not configured")
    v8_alias = alias / "DesktopEditors/core/Common/3dParty/v8_89/v8"
    if v8_alias.resolve() != V8.resolve():
        raise ValueError("Source alias does not resolve to isolated V8")
    ENV.update(DEPOT_TOOLS_UPDATE="0", DEPOT_TOOLS_WIN_TOOLCHAIN="0", GYP_MSVS_OVERRIDE_PATH="D:/Microsoft Visual Studio/18/Community",
               vs2026_install="D:/Microsoft Visual Studio/18/Community", WINDOWSSDKDIR="D:/Windows Kits/10",
               OO_V8_MSVC_TOOLS="14.44.35207", OO_V8_SDK_VERSION="10.0.26100.0", PYTHONUTF8="1", VSLANG="1033")
    ENV["PATH"] = str(tools_alias / "python-3.11.8") + os.pathsep + "D:/Qt/Tools/Ninja" + os.pathsep + ENV["PATH"]
    for name in ("TEMP", "TMP"):
        ENV[name] = str(TOOLS / "cache/tmp")
    (TOOLS / "cache/tmp").mkdir(parents=True, exist_ok=True)
    return v8_alias, tools_alias


def generate():
    v8_alias, tools_alias = build_environment()
    lastchange = V8 / "build/util/LASTCHANGE"
    if not lastchange.exists() or not Path(str(lastchange) + ".committime").exists():
        run([str(tools_alias / "python-3.11.8/python.exe"), "build/util/lastchange.py", "-o", "build/util/LASTCHANGE"], "reviewed-lastchange-hook", v8_alias)
        if lastchange.read_text().strip() == "LASTCHANGE=0":
            raise ValueError("Revision generation fell back to an invalid value")
    argv = [str(v8_alias / "buildtools/win/gn.exe"), "gen", "out.gn/win_64/release", "--script-executable=" + str(tools_alias / "python-3.11.8/python.exe")]
    result = run(argv, "gn-gen", v8_alias, check=False)
    print(result.stdout.decode("utf-8", "replace")[-10000:] + result.stderr.decode("utf-8", "replace")[-4000:], flush=True)
    if result.returncode:
        raise RuntimeError("GN generation failed; see gn-gen.log")
    apply_dependency_prefix()


def apply_dependency_prefix():
    """GN 1877 cannot emit Ninja's localized MSVC dependency prefix itself.

    Apply the experimentally verified raw-byte prefix to generated cc/cxx
    rules after each generation, leaving the resource compiler rule alone.
    Always generate/build through this script so GN regeneration is followed
    by this deterministic adaptation.
    """
    receipt = EVIDENCE / "dependency-prefix.json"
    if not receipt.exists():
        localization_probe()
    data = json.loads(receipt.read_text())
    if data["status"] != "verified":
        raise ValueError("Compiler dependency prefix has not been verified")
    prefix = bytes.fromhex(data["prefix_hex"])
    if any(token in prefix for token in (b"\r", b"\n", b"$")):
        raise ValueError("Unsupported Ninja dependency-prefix byte sequence")
    path = V8 / "out.gn/win_64/release/toolchain.ninja"
    original = path.read_bytes()
    updated = original
    for rule in (b"cc", b"cxx"):
        pattern = rb"(?m)^rule " + rule + rb"\r?\n(?:(?:[ \t].*|)\r?\n)*"
        matches = list(re.finditer(pattern, updated))
        if len(matches) != 1:
            raise ValueError("Expected one generated " + rule.decode() + " rule")
        block = matches[0].group()
        binding = b"  msvc_deps_prefix = " + prefix + b"\n"
        if binding in block:
            continue
        if b"msvc_deps_prefix" in block or b"  deps = msvc\n" not in block:
            raise ValueError("Unexpected generated compiler dependency rule")
        replacement = block.replace(b"  deps = msvc\n", b"  deps = msvc\n" + binding, 1)
        updated = updated[:matches[0].start()] + replacement + updated[matches[0].end():]
    if updated != original:
        path.write_bytes(updated)
    save(EVIDENCE / "dependency-prefix-application.json", {"at": timestamp(), "path": str(path), "before_sha256": hashlib.sha256(original).hexdigest(), "after_sha256": digest(path), "rules": ["cc", "cxx"], "prefix_hex": prefix.hex(), "encoding": data["encoding"], "resource_rule_unchanged": True})


def compile_v8():
    v8_alias, _ = build_environment()
    # Refresh the manifest before applying the prefix. Otherwise Ninja could
    # auto-run GN after an input change and replace our localized rule binding.
    generate()
    with (EVIDENCE / "ninja-build.log").open("a", encoding="utf-8") as log:
        argv = ["D:/Qt/Tools/Ninja/ninja.exe", "-C", "out.gn/win_64/release", "-j", "4", "v8_monolith"]
        log.write(json.dumps({"at": timestamp(), "argv": argv}) + "\n")
        log.flush()
        result = subprocess.run(argv, cwd=v8_alias, env=ENV, stdout=log, stderr=log)
    save(EVIDENCE / "build-result.json", {"exit_code": result.returncode, "at": timestamp(), "log": str(EVIDENCE / "ninja-build.log")})
    if result.returncode:
        raise RuntimeError("V8 build failed; see ninja-build.log")
    artifact = V8 / "out.gn/win_64/release/obj/v8_monolith.lib"
    save(EVIDENCE / "build-result.json", {"exit_code": 0, "at": timestamp(), "v8_commit": PIN, "path": str(artifact), "size": artifact.stat().st_size, "sha256": digest(artifact), "log": str(EVIDENCE / "ninja-build.log"), "compiler": "MSVC 14.44.35207", "sdk": "10.0.26100.0", "gn_args": (artifact.parents[1] / "args.gn").read_text(), "jobs": 4})
    print("V8_MONOLITH_BUILD_PASSED", flush=True)


def repair_dependencies():
    """Recompile after preserving the first pass's incomplete dependency DB."""
    output = V8 / "out.gn/win_64/release"
    if not output.resolve().is_relative_to(V8.resolve()):
        raise ValueError("Unexpected output-directory resolution")
    migration = EVIDENCE / "dependency-rebuild.json"
    if not migration.exists():
        original = output / ".ninja_deps"
        backup = output / (".ninja_deps.before-localized-prefix-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%f"))
        if not original.is_file() or backup.exists():
            raise ValueError("Cannot preserve the original dependency database")
        data = {"status": "rebuilding", "at": timestamp(), "backup_path": str(backup), "backup_sha256": digest(original), "backup_size": original.stat().st_size, "reason": "Initial MSVC output was localized; preserve original DB and recompile to populate all actual header dependencies."}
        original.rename(backup)
        save(migration, data)
    compile_v8()
    checks = []
    v8_alias, _ = build_environment()
    for target in ("obj/v8_wrappers/wrappers.obj", "obj/v8_libplatform/default-platform.obj", "obj/v8_base_without_compiler_0/api.obj"):
        result = run(["D:/Qt/Tools/Ninja/ninja.exe", "-C", "out.gn/win_64/release", "-t", "deps", target], "dependency-check-" + Path(target).stem, v8_alias)
        match = re.search(rb"#deps (\d+)", result.stdout)
        if not match or int(match.group(1)) == 0:
            raise ValueError("Missing real object header dependencies: " + target)
        checks.append({"target": target, "header_dependencies": int(match.group(1))})
    data = json.loads(migration.read_text())
    data.update(status="passed", completed_at=timestamp(), checks=checks)
    save(migration, data)
    print("V8_HEADER_DEPENDENCY_REBUILD_PASSED", flush=True)


def smoke():
    """Link the pinned official JS/Wasm sample only to our monolith + OS libs."""
    v8_alias, _ = build_environment()
    output = v8_alias / "out.gn/win_64/release"
    for pair in (output / "environment.x64").read_text(encoding="utf-8").rstrip("\0").split("\0"):
        key, value = pair.split("=", 1)
        ENV[key] = value
    compiler_root = Path("D:/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64")
    original_source = v8_alias / "samples/hello-world.cc"
    source = output / "monolith-smoke.cc"
    original = original_source.read_text(encoding="utf-8")
    marker = "    {\n      // Use the JavaScript API to generate a WebAssembly module."
    if original.count(marker) != 1:
        raise ValueError("Official embedding sample changed unexpectedly")
    intl = '''    {
      auto source = v8::String::NewFromUtf8Literal(isolate,
          "JSON.stringify([new Intl.NumberFormat('en-US', {useGrouping:false,"
          "minimumFractionDigits:1}).format(12345.5),"
          "new Intl.DateTimeFormat('en-US', {timeZone:'UTC',year:'numeric'})"
          ".format(new Date(Date.UTC(2020,0,1)))])");
      auto script = v8::Script::Compile(context, source).ToLocalChecked();
      auto result = script->Run(context).ToLocalChecked();
      v8::String::Utf8Value utf8(isolate, result);
      printf("Intl: %s\\n", *utf8);
    }

'''
    source.write_text(original.replace(marker, intl + marker).replace("  // Initialize V8.\n", '  printf("V8 version: %s\\n", v8::V8::GetVersion());\n  // Initialize V8.\n', 1), encoding="utf-8", newline="\n")
    object_path = output / "monolith-smoke.obj"
    executable = output / "monolith-smoke.exe"
    monolith = output / "obj/v8_monolith.lib"
    icudata = output / "icudtl.dat"
    if not monolith.is_file() or not icudata.is_file():
        raise ValueError("Built monolith or matching ICU runtime data missing")
    run([str(compiler_root / "cl.exe"), "/nologo", "/c", "/O2", "/MD", "/EHsc", "/std:c++14", "/DNDEBUG", "/DV8_COMPRESS_POINTERS", "/I" + str(v8_alias), "/I" + str(v8_alias / "include"), str(source), "/Fo" + str(object_path)], "monolith-smoke-compile", output)
    native_system_libraries = "advapi32.lib comdlg32.lib dbghelp.lib dnsapi.lib gdi32.lib msimg32.lib odbc32.lib odbccp32.lib oleaut32.lib shell32.lib shlwapi.lib user32.lib usp10.lib uuid.lib version.lib wininet.lib winmm.lib winspool.lib ws2_32.lib delayimp.lib kernel32.lib ole32.lib".split()
    run([str(compiler_root / "link.exe"), "/NOLOGO", "/MACHINE:X64", "/SUBSYSTEM:CONSOLE", "/INCREMENTAL:NO", "/OUT:" + str(executable), str(object_path), str(monolith)] + native_system_libraries, "monolith-smoke-link", output)
    imports = run([str(compiler_root / "dumpbin.exe"), "/DEPENDENTS", str(executable)], "monolith-smoke-dependents", output)
    imported_dlls = sorted(set(match.decode("ascii") for match in re.findall(rb"(?mi)^\s+([a-z0-9_.-]+\.dll)\s*$", imports.stdout)))
    if not imported_dlls or any("v8" in item.lower() or "cef" in item.lower() for item in imported_dlls):
        raise ValueError("Unexpected smoke executable dynamic dependencies")
    result = run([str(executable)], "monolith-smoke-run", output, timeout=60)
    text = result.stdout.decode("utf-8", "replace").replace("\r\n", "\n").strip()
    if text != 'V8 version: 8.9.255.25\nHello, World!\nIntl: ["12345.5","2020"]\n3 + 4 = 7':
        raise ValueError("Unexpected V8 JS/Wasm smoke output: " + text)
    # Generated standard targets must also be stable after the repaired build.
    clean = run(["D:/Qt/Tools/Ninja/ninja.exe", "-C", "out.gn/win_64/release", "-n", "v8_monolith"], "incremental-no-work", v8_alias)
    if b"no work to do" not in clean.stdout:
        raise ValueError("V8 generated build still has pending work")
    save(EVIDENCE / "monolith-smoke.json", {"status": "passed", "at": timestamp(), "source": str(source), "source_sha256": digest(source), "source_origin": str(original_source), "source_origin_sha256": digest(original_source), "sample_adaptation": "Official sample plus exact V8 version and Intl number/date formatting checks", "source_commit": PIN, "executable": str(executable), "executable_sha256": digest(executable), "monolith": str(monolith), "monolith_size": monolith.stat().st_size, "monolith_sha256": digest(monolith), "icudtl_path": str(icudata), "icudtl_size": icudata.stat().st_size, "icudtl_sha256": digest(icudata), "linked_non_system_libraries": [str(monolith)], "imported_dlls": imported_dlls, "stdout": text, "incremental_no_work": True, "compiler": "MSVC 14.44.35207", "sdk": "10.0.26100.0", "crt": "MD", "pointer_compression": True})
    print("V8_MONOLITH_JS_WASM_SMOKE_PASSED", flush=True)


def localization_probe():
    header = EVIDENCE / "include-probe.h"
    source = EVIDENCE / "include-probe.c"
    header.write_text("#define V8_INCLUDE_PROBE 42\n", encoding="ascii")
    source.write_text('#include "include-probe.h"\nint probe(void) { return V8_INCLUDE_PROBE; }\n', encoding="ascii")
    ENV["VSLANG"] = "1033"
    compiler = "D:/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe"
    result = run([compiler, "/nologo", "/showIncludes", "/c", str(source), "/Fo" + str(EVIDENCE / "include-probe.obj")], "include-localization-probe")
    payload = result.stdout + result.stderr
    (EVIDENCE / "include-localization-probe.raw").write_bytes(payload)
    save(EVIDENCE / "include-localization-probe.json", {"VSLANG": "1033", "english_prefix_present": b"Note: including file:" in payload, "raw_sha256": hashlib.sha256(payload).hexdigest(), "output_utf8": payload.decode("utf-8", "replace"), "output_cp936": payload.decode("cp936", "replace")})
    print(json.dumps({"english_prefix_present": b"Note: including file:" in payload}), flush=True)
    localized_line = next(line for line in payload.splitlines() if str(header).encode("cp936") in line)
    prefix = localized_line.split(str(header).encode("cp936"))[0].rstrip()
    folder = EVIDENCE / "dependency-prefix-probe"
    folder.mkdir(exist_ok=True)
    (folder / "probe.h").write_text("#define PREFIX_PROBE 42\n", encoding="ascii")
    (folder / "probe.c").write_text('#include "probe.h"\nint probe(void) { return PREFIX_PROBE; }\n', encoding="ascii")
    ninja = ('rule cc\n  command = "' + compiler + '" /nologo /showIncludes /c $in /Fo$out\n  deps = msvc\n  msvc_deps_prefix = ').encode("ascii") + prefix + b"\nbuild probe.obj: cc probe.c\n"
    (folder / "build.ninja").write_bytes(ninja)
    run(["D:/Qt/Tools/Ninja/ninja.exe", "-C", str(folder)], "dependency-prefix-probe")
    output = run(["D:/Qt/Tools/Ninja/ninja.exe", "-C", str(folder), "-t", "deps", "probe.obj"], "dependency-prefix-result").stdout
    if b"probe.h" not in output or b"#deps 0" in output:
        raise ValueError("Ninja did not record the localized include dependency")
    save(EVIDENCE / "dependency-prefix.json", {"status": "verified", "prefix_hex": prefix.hex(), "encoding": "cp936", "prefix_decoded": prefix.decode("cp936"), "VSLANG": "1033", "ninja_deps_output": output.decode("utf-8", "replace"), "existing_build_note": "Initial monolith pass started before prefix fix; subsequent generation must include this prefix."})
    print("Localized MSVC dependency prefix verified with a real header dependency", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("sync", "python-metadata", "tools", "gn", "prepare", "generate", "build", "localization-probe", "repair-deps", "smoke"))
    parser.add_argument("--group", choices=("all", "bootstrap", "remaining"), default="all")
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    TOOLS.mkdir(parents=True, exist_ok=True)
    if args.action == "sync":
        sync(args.group)
    elif args.action == "python-metadata":
        python_metadata()
    elif args.action == "tools":
        tools()
    elif args.action == "gn":
        gn()
    elif args.action == "prepare":
        prepare()
    elif args.action == "generate":
        generate()
    elif args.action == "build":
        compile_v8()
    elif args.action == "repair-deps":
        repair_dependencies()
    elif args.action == "smoke":
        smoke()
    else:
        localization_probe()


if __name__ == "__main__":
    main()
