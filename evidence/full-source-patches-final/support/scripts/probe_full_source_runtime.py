"""Load the staged source-built engine DLLs in a fresh isolated Python process.

The stage manifest controls the targets. This checks Windows loader resolution,
not document editing or conversion. It never starts the host, CEF helper, plugin
manager, or converter, and never changes global search paths or system settings.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "build/full-source-run"
MANIFEST = RUNTIME / "openoffice-full-source-stage.json"
CORE = ROOT / "build/full-source/DesktopEditors/core"
QT = Path("D:/Qt/6.11.1/msvc2022_64")
ORIGINS = {
    "source_built_native_core": CORE / "build/lib/win_64",
    "source_built_qt_wrapper": ROOT / "build/full-source-qt6",
    "source_built_icu": CORE / "Common/3dParty/icu/win_64/build",
    "official_prebuilt_cef": CORE / "Common/3dParty/cef/win_64/build",
    "installed_qt_6_11_1": QT,
}
TARGET_CATEGORIES = {"source_built_native_core", "source_built_qt_wrapper"}
OLD_ROOTS = (ROOT / "build/native-engine-run", ROOT / "build/native-engine",
             ROOT / "build/native-onlyoffice-qt6", ROOT / "dependencies/onlyoffice-v10.0.0/runtime")


def stamp():
    return datetime.now(timezone.utc).isoformat()


def normalized(path):
    return os.path.normcase(str(Path(path).resolve()))


def inside(path, root):
    return Path(path).resolve().is_relative_to(Path(root).resolve())


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class Hasher:
    def __init__(self):
        self.cache = {}

    def __call__(self, path):
        path = Path(path)
        stat = path.stat()
        key = (normalized(path), stat.st_size, stat.st_mtime_ns)
        if key not in self.cache:
            with path.open("rb") as stream:
                self.cache[key] = hashlib.file_digest(stream, "sha256").hexdigest()
        return self.cache[key]


def manifest_inputs(hasher):
    if not RUNTIME.is_dir() or RUNTIME.absolute() != RUNTIME.resolve():
        raise ValueError("The new runtime is absent or redirected; no fallback is allowed")
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or data.get("staged") is not True or normalized(data.get("target", "")) != normalized(RUNTIME):
        raise ValueError("Expected a completed schema-1 stage manifest for build/full-source-run")
    if data.get("status") != "staged_pending_runtime_validation":
        raise ValueError("Manifest is not a successfully assembled runtime stage")
    records = {}
    for item in data["files"]:
        raw = str(item["destination"]).replace("\\", "/")
        relative = PurePosixPath(raw)
        if relative.is_absolute() or ".." in relative.parts or ":" in raw or any(part.endswith((".", " ")) for part in relative.parts):
            raise ValueError("Unsafe stage destination: " + raw)
        if relative.suffix.lower() != ".dll":
            continue
        key = relative.as_posix().casefold()
        if key in records:
            raise ValueError("Duplicate native stage destination: " + raw)
        category = item["category"]
        if category not in ORIGINS:
            raise ValueError("Unapproved DLL provenance category: " + category)
        origin = ORIGINS[category]
        if origin.absolute() != origin.resolve() or normalized(item["origin_root"]) != normalized(origin):
            raise ValueError("DLL origin root differs from the approved build: " + raw)
        source, staged = Path(item["source"]), RUNTIME / relative
        if not inside(source, origin) or not inside(staged, RUNTIME):
            raise ValueError("DLL escapes its source or runtime root: " + raw)
        if not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]):
            raise ValueError("Invalid artifact hash: " + raw)
        for candidate in (source, staged):
            if not candidate.is_file() or candidate.stat().st_size != item["bytes"] or hasher(candidate) != item["sha256"]:
                raise ValueError("DLL missing or hash/size mismatch: " + str(candidate))
        records[key] = item
    targets = {Path(key).name.casefold(): item for key, item in records.items()
               if item["category"] in TARGET_CATEGORIES and PurePosixPath(key).parent == PurePosixPath(".")}
    required = {"ascdocumentscore.dll", "hunspell.dll", "openoffice_qt6_documentscore.dll", "openoffice_qt6_videoplayer.dll"}
    if not required.issubset(targets):
        raise ValueError("Stage lacks core engine/Hunspell/Qt-wrapper targets")
    # Copies under converter must be identical to the root DLL selected for load.
    for key, item in records.items():
        name = PurePosixPath(key).name.casefold()
        if item["category"] in TARGET_CATEGORIES and (name not in targets or targets[name]["sha256"] != item["sha256"]):
            raise ValueError("Conflicting or rootless core DLL: " + key)
    order, visiting, visited = [], set(), set()

    def visit(name):
        if name in visited or name in visiting:
            return
        visiting.add(name)
        for imported in targets[name].get("pe_imports", []):
            if imported.casefold() in targets:
                visit(imported.casefold())
        visiting.remove(name)
        visited.add(name)
        order.append(targets[name])

    for name in sorted(targets, key=lambda value: (targets[value]["category"] == "source_built_qt_wrapper", value)):
        visit(name)
    return data, records, targets, order


class WindowsLoader:
    def __init__(self):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        declarations = {
            "GetCurrentProcess": (wintypes.HANDLE, []),
            "K32EnumProcessModulesEx": (wintypes.BOOL, [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]),
            "GetModuleFileNameW": (wintypes.DWORD, [wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]),
            "GetWindowsDirectoryW": (wintypes.UINT, [wintypes.LPWSTR, wintypes.UINT]),
            "SetDefaultDllDirectories": (wintypes.BOOL, [wintypes.DWORD]),
            "AddDllDirectory": (ctypes.c_void_p, [wintypes.LPCWSTR]),
            "LoadLibraryExW": (wintypes.HMODULE, [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]),
        }
        for name, (restype, argtypes) in declarations.items():
            function = getattr(self.api, name)
            function.restype, function.argtypes = restype, argtypes
        buffer = ctypes.create_unicode_buffer(32768)
        if not self.api.GetWindowsDirectoryW(buffer, len(buffer)):
            raise ctypes.WinError(ctypes.get_last_error())
        windows = Path(buffer.value)
        self.system_roots = [windows / "System32", windows / "WinSxS"]
        self.cookies = []

    def module_path(self, handle):
        buffer = ctypes.create_unicode_buffer(32768)
        count = self.api.GetModuleFileNameW(handle, buffer, len(buffer))
        if not count or count >= len(buffer):
            raise ctypes.WinError(ctypes.get_last_error())
        return Path(buffer.value).resolve()

    def modules(self):
        capacity = 256
        while True:
            modules = (wintypes.HMODULE * capacity)()
            needed = wintypes.DWORD()
            if not self.api.K32EnumProcessModulesEx(self.api.GetCurrentProcess(), modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
                raise ctypes.WinError(ctypes.get_last_error())
            if needed.value <= ctypes.sizeof(modules):
                return [self.module_path(handle) for handle in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]]
            capacity = needed.value // ctypes.sizeof(wintypes.HMODULE) + 16

    def restrict_search(self):
        # User directories + System32. Neither PATH nor the working directory is
        # used; these APIs affect this disposable probe process only.
        if not self.api.SetDefaultDllDirectories(0x400 | 0x800):
            raise ctypes.WinError(ctypes.get_last_error())
        for directory in (RUNTIME, QT / "bin"):
            cookie = self.api.AddDllDirectory(str(directory.resolve()))
            if not cookie:
                raise ctypes.WinError(ctypes.get_last_error())
            self.cookies.append(cookie)

    def load(self, path):
        ctypes.set_last_error(0)
        handle = self.api.LoadLibraryExW(str(path.resolve()), None, 0x100 | 0x400 | 0x800)
        error = ctypes.get_last_error()
        return handle, error


def worker(report_path, run_id):
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("run_id") != run_id:
        raise ValueError("Probe report belongs to another run")
    hasher = Hasher()
    try:
        manifest, records, targets, order = manifest_inputs(hasher)
        report.update(manifest_sha256=hasher(MANIFEST), application_state=manifest["application_state"],
                      validated_dll_count=len(records), target_count=len(order), load_attempts=[], loaded_modules=[])
        loader = WindowsLoader()
        baseline = loader.modules()
        baseline_names = {path.name.casefold() for path in baseline}
        staged_names = {PurePosixPath(key).name.casefold() for key in records}
        if staged_names & baseline_names:
            raise ValueError("A staged DLL name was already loaded before this fresh-process probe: " + ", ".join(sorted(staged_names & baseline_names)))
        baseline_paths = {normalized(path) for path in baseline}
        python_roots = [Path(sys.base_prefix).resolve(), Path(sys.executable).resolve().parent]
        strict_names = {PurePosixPath(key).name.casefold() for key, item in records.items()
                        if item["category"].startswith("source_built_")}

        def audit_modules():
            values, violations = [], []
            for path in loader.modules():
                item = {"path": str(path), "name": path.name, "baseline": normalized(path) in baseline_paths}
                try:
                    item["sha256"] = hasher(path)
                    item["bytes"] = path.stat().st_size
                    if any(inside(path, old) for old in OLD_ROOTS):
                        raise ValueError("Old prebuilt/fallback directory loaded")
                    if inside(path, RUNTIME):
                        key = path.relative_to(RUNTIME.resolve()).as_posix().casefold()
                        if key not in records or item["sha256"] != records[key]["sha256"]:
                            raise ValueError("Loaded runtime module is absent from or differs from stage manifest")
                        item["origin"] = records[key]["category"]
                    elif path.name.casefold() in strict_names:
                        raise ValueError("Source-built DLL loaded outside the new runtime")
                    elif inside(path, QT):
                        matches = [record for record in records.values() if record["category"] == "installed_qt_6_11_1"
                                   and PurePosixPath(record["destination"]).name.casefold() == path.name.casefold()]
                        if not matches or not any(record["sha256"] == item["sha256"] for record in matches):
                            raise ValueError("Qt SDK module does not match a staged Qt dependency")
                        item["origin"] = "approved_qt_sdk_same_bytes_as_stage"
                    elif any(inside(path, directory) for directory in loader.system_roots):
                        item["origin"] = "windows_system"
                    elif item["baseline"] and any(inside(path, directory) for directory in python_roots):
                        item["origin"] = "python_probe_baseline_only"
                    else:
                        raise ValueError("Module path is outside runtime, approved Qt, Windows and Python baseline")
                except (OSError, ValueError) as error:
                    item["error"] = str(error)
                    violations.append(item)
                values.append(item)
            report["loaded_modules"], report["origin_violations"] = values, violations
            return violations

        report["search_directories"] = [str(RUNTIME), str(QT / "bin"), *map(str, loader.system_roots)]
        report["python_baseline_roots"] = list(map(str, python_roots))
        if audit_modules():
            raise ValueError("Unexpected module in fresh-process baseline")
        loader.restrict_search()
        report["status"] = "loading"
        save(report_path, report)
        for entry in order:
            path = RUNTIME / entry["destination"]
            attempt = {"requested_path": str(path), "expected_sha256": entry["sha256"], "imports": entry.get("pe_imports", []), "started_at_utc": stamp()}
            report["load_attempts"].append(attempt)
            save(report_path, report)
            handle, error = loader.load(path)
            attempt["loaded"] = bool(handle)
            if handle:
                actual = loader.module_path(handle)
                attempt.update(actual_path=str(actual), sha256=hasher(actual))
                if normalized(actual) != normalized(path) or attempt["sha256"] != entry["sha256"]:
                    raise ValueError("Windows returned a different same-name DLL for " + str(path))
            else:
                attempt.update(winerror=error, error=ctypes.FormatError(error).strip())
            if audit_modules():
                raise ValueError("Loaded module provenance violation; further loading stopped")
            save(report_path, report)
        report["loader_passed"] = all(item["loaded"] for item in report["load_attempts"])
        report["status"] = "loader_passed_functional_regression_not_run" if report["loader_passed"] else "loader_failed"
    except Exception as error:
        report["status"] = "loader_failed"
        report["error"] = str(error)
        report["loader_passed"] = False
    report["completed_at_utc"] = stamp()
    save(report_path, report)
    print(json.dumps({"status": report["status"], "loader_passed": report["loader_passed"], "report": str(report_path)}), flush=True)
    return 0 if report["loader_passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--run-id", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8:
        parser.error("Use the existing 64-bit Windows Python interpreter")
    if not 1 <= args.timeout <= 600:
        parser.error("timeout must be between 1 and 600 seconds")
    report_path = (args.report or ROOT / "evidence" / ("full-source-runtime-probe-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + ".json")).resolve()
    if not inside(report_path, ROOT) or inside(report_path, RUNTIME):
        parser.error("Keep the probe report in this workspace, outside the staged runtime")
    if args.worker:
        return worker(report_path, args.run_id)
    if report_path.exists():
        raise FileExistsError("Use a new report filename; previous evidence is preserved")
    if not MANIFEST.is_file():
        raise FileNotFoundError("Stage the complete new engine-only runtime before running this probe: " + str(MANIFEST))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    run_id = uuid.uuid4().hex
    report = {"schema_version": 1, "run_id": run_id, "started_at_utc": stamp(), "status": "worker_starting",
              "manifest": str(MANIFEST), "runtime": str(RUNTIME), "loader_passed": False,
              "functional_regression_executed": False, "docx_roundtrip_verified": False,
              "scope": "DLL loading and origin audit only; Python bootstrap DLLs are explicitly separate from tested engine DLLs"}
    save(report_path, report)
    environment = os.environ.copy()
    for name in ("PYTHONPATH", "PYTHONHOME", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH"):
        environment.pop(name, None)
    # The worker additionally sets the Windows loader's process-local search mode.
    environment["PATH"] = os.pathsep.join([str(RUNTIME), str(QT / "bin"), str(Path(os.environ["SystemRoot"]) / "System32")])
    argv = [sys.executable, "-I", "-S", str(Path(__file__).resolve()), "--worker", "--report", str(report_path), "--run-id", run_id]
    try:
        result = subprocess.run(argv, cwd=RUNTIME, env=environment, capture_output=True, timeout=args.timeout)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        report["worker_exit_code"] = result.returncode
        report["worker_stdout"] = result.stdout.decode("utf-8", "replace")[-6000:]
        report["worker_stderr"] = result.stderr.decode("utf-8", "replace")[-6000:]
        if result.returncode and report.get("status") not in ("loader_failed",):
            report.update(status="worker_failed", loader_passed=False)
    except subprocess.TimeoutExpired:
        report = json.loads(report_path.read_text(encoding="utf-8"))
        report.update(status="worker_timeout", loader_passed=False, timeout_seconds=args.timeout)
    report["controller_completed_at_utc"] = stamp()
    save(report_path, report)
    print(json.dumps({"status": report["status"], "loader_passed": report["loader_passed"], "report": str(report_path)}), flush=True)
    return 0 if report["loader_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
