"""Prepare pinned Socket.IO source dependencies without upstream make/cleanup.

Only the isolated full-source tree and dedicated evidence directory are changed.
Git settings are process-local; no installer or global/system config is used.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/full-source"
THIRD = WORK / "DesktopEditors/core/Common/3dParty/socketio"
CLIENT = THIRD / "socket.io-client-cpp"
EVIDENCE = WORK / "socket-dependency-evidence"
MANIFEST = EVIDENCE / "sources.json"
SOURCES = {
    "socketio": (CLIENT, "https://github.com/socketio/socket.io-client-cpp.git", "da779141a7379cc30c870d48295033bc16a23c66"),
    "asio": (CLIENT / "lib/asio", "https://github.com/chriskohlhoff/asio.git", "230c0d2ae035c5ce1292233fcab03cea0d341264"),
    "websocketpp": (CLIENT / "lib/websocketpp", "https://github.com/zaphoyd/websocketpp.git", "56123c87598f8b1dd471be83ca841ceae07f95ba"),
}
ENV = os.environ.copy()
ENV.update(GIT_TERMINAL_PROMPT="0", GIT_OPTIONAL_LOCKS="0", GIT_NO_LAZY_FETCH="1")


def stamp():
    return datetime.now(timezone.utc).isoformat()


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def command(*args, cwd=None, optional=False):
    argv = ["git", "-c", "core.hooksPath=" + str(WORK / "empty-hooks"),
            "-c", "core.longpaths=true", "-c", "core.autocrlf=false",
            "-c", "http.version=HTTP/1.1", *args]
    result = subprocess.run(argv, cwd=cwd, env=ENV, capture_output=True, timeout=300)
    with (EVIDENCE / "git.log").open("a", encoding="utf-8") as log:
        log.write(json.dumps({"at": stamp(), "argv": argv, "cwd": str(cwd), "returncode": result.returncode}) + "\n")
        log.write(result.stdout.decode("utf-8", "replace") + result.stderr.decode("utf-8", "replace") + "\n")
    if result.returncode and not optional:
        raise RuntimeError(result.stderr.decode("utf-8", "replace")[-4000:])
    return result


def text(*args, cwd=None):
    return command(*args, cwd=cwd).stdout.decode("utf-8").strip()


def acquire_one(data, name, location, url, commit):
    if not location.resolve().is_relative_to(THIRD.resolve()):
        raise ValueError("Destination escapes isolated socket source directory")
    record = data.setdefault(name, {"repository": url, "commit": commit, "directory": str(location)})
    if record["repository"] != url or record["commit"] != commit:
        raise ValueError("Recorded source does not match fixed source")
    save(MANIFEST, data)
    if not (location / ".git").is_dir():
        if location.exists() and any(location.iterdir()):
            raise FileExistsError("Unowned nonempty directory: " + str(location))
        location.mkdir(parents=True, exist_ok=True)
        command("init", "--initial-branch=source-build", str(location))
        command("remote", "add", "origin", url, cwd=location)
    if text("remote", "get-url", "origin", cwd=location) != url:
        raise ValueError("Existing source origin differs")
    head = command("rev-parse", "--verify", "HEAD", cwd=location, optional=True)
    if head.returncode:
        command("fetch", "--depth=1", "origin", commit, cwd=location)
        command("checkout", "--detach", commit, cwd=location)
    elif head.stdout.decode().strip() != commit:
        raise ValueError("Existing checkout has a different commit; refusing overwrite")
    record.update(checked_out_commit=text("rev-parse", "HEAD", cwd=location), status="checked_out", checked_at_utc=stamp())
    save(MANIFEST, data)
    print(name + " verified " + commit, flush=True)


def acquire():
    data = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    acquire_one(data, "socketio", *SOURCES["socketio"])
    modules = command("show", SOURCES["socketio"][2] + ":.gitmodules", cwd=CLIENT).stdout.decode()
    for name in ("asio", "websocketpp"):
        location, url, commit = SOURCES[name]
        if url not in modules:
            raise ValueError("Official submodule source differs: " + name)
        acquire_one(data, name, location, url, commit)
    rapid = text("ls-tree", SOURCES["socketio"][2], "lib/rapidjson", cwd=CLIENT).split()
    if len(rapid) != 4 or rapid[:2] != ["160000", "commit"]:
        raise ValueError("RapidJSON gitlink was not found")
    rapid_url = "https://github.com/miloyip/rapidjson.git"
    if rapid_url not in modules:
        raise ValueError("RapidJSON URL differs from upstream .gitmodules")
    acquire_one(data, "rapidjson", CLIENT / "lib/rapidjson", rapid_url, rapid[2])
    data["rapidjson"]["commit_source"] = "socketio commit gitlink lib/rapidjson"
    save(MANIFEST, data)


def original(repository, relative):
    return command("show", "HEAD:" + relative, cwd=repository).stdout


def prepare():
    data = json.loads(MANIFEST.read_text())
    for record in data.values():
        if text("rev-parse", "HEAD", cwd=Path(record["directory"])) != record["commit"]:
            raise ValueError("Source revision mismatch")
    files = {}
    for line in text("ls-tree", "-r", "--name-only", "HEAD", "src", cwd=CLIENT).splitlines():
        files[line] = original(CLIENT, line).decode("utf-8")
    ws_rel = "websocketpp/impl/connection_impl.hpp"
    ws_file = CLIENT / "lib/websocketpp" / ws_rel
    ws_original = original(CLIENT / "lib/websocketpp", ws_rel).decode("utf-8")
    content = {str(ws_file): ws_original, str(CLIENT / "src/internal/sio_client_impl.cpp"): files["src/internal/sio_client_impl.cpp"]}
    patches = []
    names = ["websocketpp", "sio_client_impl_fail", "sio_client_impl_open", "sio_client_impl_close_timeout", "sio_client_impl_encode"]
    for name in names:
        patch_file = THIRD / "patches" / (name + ".patch")
        patch_bytes = patch_file.read_bytes()
        patch_text = patch_bytes.decode("utf-8").replace("\r\n", "\n")
        old = patch_text.split("<<<<<<<", 1)[1].split("=======", 1)[0].strip()
        new = patch_text.split("=======", 1)[1].split(">>>>>>>", 1)[0].strip()
        path = str(ws_file if name == "websocketpp" else CLIENT / "src/internal/sio_client_impl.cpp")
        current = content[path].replace("\r\n", "\n")
        record = {"name": name, "patch_file": str(patch_file), "patch_sha256": sha(patch_bytes), "before_sha256": sha(current.encode())}
        if current.count(old) == 0 and name == "sio_client_impl_fail":
            candidate = old.replace("sio_no_tls::", "sio::")
            if candidate == old or current.count(candidate) != 1:
                raise ValueError("Failure patch cannot be matched exactly")
            old = candidate
            record["anchor_adjustment"] = "Upstream patch old block refers to sio_no_tls; pristine src uses sio. Match namespace only; replacement remains exactly upstream."
        if current.count(old) != 1:
            raise ValueError("Expected one exact anchor: " + name)
        current = current.replace(old, new, 1)
        content[path] = current
        record["after_sha256"] = sha(current.encode())
        patches.append(record)
    files["src/internal/sio_client_impl.cpp"] = content[str(CLIENT / "src/internal/sio_client_impl.cpp")]
    prepared = {CLIENT / relative: value.replace("\r\n", "\n") for relative, value in files.items()}
    prepared[ws_file] = content[str(ws_file)]
    for relative, source in files.items():
        target = CLIENT / ("src_no_tls/" + relative[len("src/"):])
        value = source.replace("\r\n", "\n").replace("namespace sio", "namespace sio_no_tls").replace("asio::", "asio_no_tls::").replace("sio::", "sio_no_tls::").replace("asio_no_tls::", "asio::")
        if relative in ("src/internal/sio_client_impl.h", "src/internal/sio_client_impl.cpp"):
            value = value.replace("SIO_TLS", "SIO_TLS_NO")
        guard = {"src/sio_socket.h": ("SIO_SOCKET_H", "SIO_SOCKET_NO_TLS_H"), "src/sio_client.h": ("SIO_CLIENT_H", "SIO_CLIENT_NO_TLS_H"), "src/sio_message.h": ("__SIO_MESSAGE_H__", "__SIO_MESSAGE_NO_TLS_H__"), "src/internal/sio_packet.h": ("SIO_PACKET_H", "SIO_PACKET_NO_TLS_H")}.get(relative)
        if guard:
            value = value.replace(*guard)
        prepared[target] = value
    old_ping = "        m_ping_timeout_timer->expires_from_now(milliseconds(m_ping_interval + m_ping_timeout), ec);"
    new_ping = "#if defined(PING_TIMEOUT_INTERVAL)\n        m_ping_timeout_timer->expires_from_now(milliseconds(PING_TIMEOUT_INTERVAL), ec);\n#else\n" + old_ping + "\n#endif"
    for kind in ("src", "src_no_tls"):
        path = CLIENT / kind / "internal/sio_client_impl.cpp"
        if prepared[path].count(old_ping) != 1:
            raise ValueError("Ping timeout anchor is not unique")
        prepared[path] = prepared[path].replace(old_ping, new_ping, 1)
    receipt = EVIDENCE / "preparation.json"
    previous = json.loads(receipt.read_text()) if receipt.exists() else {}
    known_hashes = {row["path"]: row["sha256"] for row in previous.get("files", [])}
    for path, value in prepared.items():
        if path.exists():
            current = path.read_bytes()
            normalized = current.decode("utf-8").replace("\r\n", "\n")
            baseline = ws_original if path == ws_file else files.get(path.relative_to(CLIENT).as_posix())
            if path == CLIENT / "src/internal/sio_client_impl.cpp":
                baseline = original(CLIENT, "src/internal/sio_client_impl.cpp").decode("utf-8")
            allowed = [value]
            if baseline is not None:
                allowed.append(baseline.replace("\r\n", "\n"))
            if normalized not in allowed and sha(current) != known_hashes.get(str(path)):
                raise ValueError("Refusing to overwrite unrelated edits: " + str(path))
    records = []
    for path, value in prepared.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        expected = value.encode("utf-8")
        if not path.exists() or path.read_bytes() != expected:
            path.write_bytes(expected)
        records.append({"path": str(path), "sha256": sha(expected), "size": len(expected)})
    checks = [CLIENT / "lib/asio/asio/include/asio.hpp", CLIENT / "lib/rapidjson/include/rapidjson/document.h", CLIENT / "lib/websocketpp/websocketpp/client.hpp"]
    for path in checks:
        if not path.is_file():
            raise ValueError("Required header missing: " + str(path))
    save(receipt, {"status": "prepared", "at_utc": stamp(), "upstream_recipe": "build_tools/scripts/core_common/modules/socket_io.py", "patches": patches, "files": records, "required_headers": [str(p) for p in checks], "no_tls_source_count": sum("src_no_tls" in str(p) for p in prepared)})
    print("Socket dependencies prepared; " + str(len(records)) + " files verified", flush=True)


def verify():
    receipt = json.loads((EVIDENCE / "preparation.json").read_text())
    for record in receipt["files"]:
        path = Path(record["path"])
        if not path.resolve().is_relative_to(CLIENT.resolve()) or sha(path.read_bytes()) != record["sha256"]:
            raise ValueError("Prepared file hash or path mismatch: " + str(path))
    core = WORK / "DesktopEditors/core"
    library_dir = core / "build/lib/win_64"
    log_dir = WORK / "core-evidence/network-socket-ready"
    makefile = (WORK / "core-builds/network/Makefile").read_text(encoding="utf-8")
    if "localipc.cpp" not in makefile or "USE_IOWEBSOCKET" in makefile:
        raise ValueError("Build does not match the requested desktop default configuration")
    dumpbin = Path("D:/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe")
    artifact = library_dir / "kernel_network.dll"
    reports = {}
    for option in ("headers", "dependents", "exports"):
        result = subprocess.run([str(dumpbin), "/" + option, str(artifact)], capture_output=True, check=True)
        reports[option] = result.stdout.decode("utf-8", "replace")
        (EVIDENCE / ("kernel-network-" + option + ".txt")).write_bytes(result.stdout)
    if "8664 machine (x64)" not in reports["headers"]:
        raise ValueError("Network DLL is not an x64 artifact")
    for symbol in ("createLocalIpcServer", "createLocalIpcClient"):
        if symbol not in reports["exports"]:
            raise ValueError("Missing local IPC factory export: " + symbol)
    loader_handles = [os.add_dll_directory(str(library_dir)), os.add_dll_directory(str(core / "Common/3dParty/icu/win_64/build"))]
    loaded = ctypes.WinDLL(str(artifact))
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    get_module = kernel32.GetModuleHandleW
    get_module.argtypes = [ctypes.c_wchar_p]
    get_module.restype = ctypes.c_void_p
    get_filename = kernel32.GetModuleFileNameW
    get_filename.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint]
    get_filename.restype = ctypes.c_uint
    loaded_paths = {}
    for name in ("kernel_network.dll", "kernel.dll", "UnicodeConverter.dll"):
        module = get_module(name)
        if not module:
            if name == "kernel_network.dll":
                raise ValueError("Network DLL failed to load")
            continue
        buffer = ctypes.create_unicode_buffer(32768)
        if not get_filename(module, buffer, len(buffer)):
            raise ctypes.WinError(ctypes.get_last_error())
        path = Path(buffer.value)
        if not path.resolve().is_relative_to(library_dir.resolve()):
            raise ValueError("Unexpected core DLL loaded outside isolated library directory")
        loaded_paths[name] = str(path)
    artifacts = [{"path": str(path), "size": path.stat().st_size, "sha256": sha(path.read_bytes())}
                 for path in (artifact, library_dir / "kernel_network.lib", library_dir / "kernel_network.exp")]
    save(EVIDENCE / "build-verification.json", {
        "status": "passed", "verified_at_utc": stamp(), "source_files_hash_verified": len(receipt["files"]),
        "command": "scripts/build_full_source_core.cmd network socket-ready", "build_logs": str(log_dir),
        "configuration": "win_64/release desktop", "msvc_tools": "14.44.35207",
        "windows_sdk_observed_in_build_log": "10.0.28000.0" if "10.0.28000.0" in (log_dir / "build.log").read_text(errors="replace") else None,
        "optional_websocket_enabled": False, "local_ipc_compiled": True,
        "local_ipc_factory_exports_verified": True, "load_library_passed": True,
        "loaded_core_paths": loaded_paths, "artifacts": artifacts,
        "limitations": ["Local IPC request-response behavior not exercised.", "Optional TLS/plain WebSocket implementations prepared but not compiled by desktop default."]})
    print("kernel_network: x64 DLL, IPC exports, prepared hashes, isolated DLL loading verified", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("acquire", "prepare", "verify"))
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    if args.action == "acquire":
        acquire()
    elif args.action == "prepare":
        prepare()
    else:
        verify()


if __name__ == "__main__":
    main()
