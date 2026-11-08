"""Inventory or stage the isolated source build, never the old prebuilt core.

Default inventories inputs and writes its JSON report. --engine-only
creates build/full-source-run without the host so CMake can link the new host.
--finish-host verifies that stage and adds build/full-source-host/bin/OpenOffice.exe.
No executable from the assembled runtime is launched by this script.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import subprocess
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "build/full-source"
SOURCE = AREA / "DesktopEditors"
CORE = SOURCE / "core"
LIB = CORE / "build/lib/win_64"
BIN = CORE / "build/bin/win_64"
WRAPPERS = ROOT / "build/full-source-qt6"
HOST = ROOT / "build/full-source-host"
QT = Path("D:/Qt/6.11.1/msvc2022_64")
QT_SIMULATION = AREA / "desktop-build-evidence"
TARGET = ROOT / "build/full-source-run"
MARKER_NAME = "openoffice-full-source-stage.json"
CEF = CORE / "Common/3dParty/cef/win_64/build"
CEF_RECEIPT = AREA / "desktop-build-evidence/cef-stage.json"
CEF_ARCHIVE = ROOT / "dependencies/onlyoffice-cef-5414-win64/cef_binary.7z"
CEF_SHA256 = "0ef79ad22cc1b2e9364e6a165d1ee00f9348b46f6bb94615a48ed402145f741a"
RESOURCE_ARCHIVE = ROOT / "dependencies/onlyoffice-v10.0.0/DesktopEditors_x64.zip"
RESOURCE_SHA256 = "54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605"
CORE_DLLS = (
    "graphics", "kernel", "UnicodeConverter", "kernel_network", "Fb2File",
    "PdfFile", "HtmlFile2", "EpubFile", "XpsFile", "OFDFile", "DjVuFile",
    "doctrenderer", "DocxRenderer", "IWorkFile", "HWPFile", "StarMathConverter",
    "ooxmlsignature",
)
STATIC_LIBS = (
    "VbaFormatLib", "OdfFormatLib", "DocFormatLib", "PptFormatLib", "RtfFormatLib",
    "TxtXmlFormatLib", "BinDocument", "PPTXFormatLib", "DocxFormatLib",
    "XlsbFormatLib", "XlsFormatLib", "CompoundFileLib", "CryptoPPLib",
)
CEF_ROOT_FILES = {
    "libcef.dll", "chrome_elf.dll", "d3dcompiler_47.dll", "libEGL.dll",
    "libGLESv2.dll", "vk_swiftshader.dll", "vulkan-1.dll", "snapshot_blob.bin",
    "v8_context_snapshot.bin", "vk_swiftshader_icd.json", "chrome_100_percent.pak",
    "chrome_200_percent.pak", "resources.pak", "icudtl.dat",
}
QT_MODULES = ("Core", "Gui", "Widgets", "PrintSupport", "Multimedia",
              "MultimediaWidgets", "Network", "Svg")
FORBIDDEN_RESOURCE_SUFFIXES = {".dll", ".exe", ".com", ".sys", ".lib", ".pdb",
                               ".bat", ".cmd", ".ps1", ".msi", ".lnk"}
DOCT_CONFIG = (
    "<Settings>\n"
    "<file>../editors/sdkjs/common/Native/native.js</file>\n"
    "<file>../editors/sdkjs/common/Native/jquery_native.js</file>\n"
    "<allfonts>../editors/sdkjs/common/AllFonts.js</allfonts>\n"
    "<file>../editors/web-apps/vendor/xregexp/xregexp-all-min.js</file>\n"
    "<sdkjs>../editors/sdkjs</sdkjs>\n"
    "<dictionaries>../dictionaries</dictionaries>\n</Settings>\n"
)
STAGE_NOTICES = """# Open Office local source-build stage

This is an independent local development build, not an official ONLYOFFICE
distribution. No endorsement or trademark rights are claimed. Copyright (C)
Ascensio System SIA, 2009-2026. Original editor notices and attribution are retained.

The native ONLYOFFICE core, converter, helper, and Qt6 wrappers in this directory
come from the project's isolated pinned-source builds. The own application entry
point is added in a separate build step. The per-file origin, source commits,
size, and SHA-256 are recorded in openoffice-full-source-stage.json. Assembly
does not establish that editing, saving, or reopening a document has passed.

CEF 109 and Qt 6.11.1 are explicitly identified third-party binary dependencies.
Microsoft's installed C/C++ runtime is used; no installer is run by this staging
script. SDKJS and web-apps come from this source build's deployment outputs;
bundled WASM and other pre-generated assets have not all been recompiled.
Fonts, font license texts, blank templates, and the connection-error page come
from the checksum-verified official v10.0.0 archive, with a strict resource-only
allowlist. Native core binaries and converter executables are never copied from
that archive or the old runtime.

ONLYOFFICE source licensing and additional terms remain in the accompanying
OPEN_OFFICE_SOURCE_LICENSES directory. Preserve those files and the upstream
interactive legal notices. Component licenses and source availability must be
reviewed before external distribution; this directory is for local verification.
"""


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def relative_name(value):
    """A portable Windows destination, including when checked from another OS."""
    value = str(value).replace("\\", "/")
    parts = PurePosixPath(value)
    if (not value or parts.as_posix() == "." or parts.is_absolute() or ":" in value or ".." in parts.parts
            or any(p.endswith((".", " ")) for p in parts.parts)
            or any(re.match(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", p, re.I)
                   for p in parts.parts)):
        raise ValueError("Unsafe relative path: " + value)
    return parts.as_posix()


def checked_source(path, allowed):
    path, allowed = Path(path), Path(allowed)
    # Do not allow replacing an entire approved origin with a junction to fallback.
    if allowed.absolute() != allowed.resolve():
        raise ValueError("Origin root is redirected: " + str(allowed))
    if not path.resolve().is_relative_to(allowed.resolve()):
        raise ValueError("Source escapes its approved origin: " + str(path))
    return path


def resource_member_allowed(name):
    name = relative_name(name)
    suffix = PurePosixPath(name).suffix.lower()
    return ((name.startswith("fonts/") and suffix in {".ttf", ".ttc", ".otf", ".txt"})
            or (name.startswith("converter/empty/") and suffix in {".docx", ".xlsx", ".pptx", ".pdf"})
            or name == "editors/webext/noconnect.html")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def cache_values(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


class Inventory:
    def __init__(self):
        self.files = {}
        self.errors = []
        self.inputs = []
        self.provenance = {}
        self._hashes = {}

    def error(self, message):
        self.errors.append(str(message))

    def hashed(self, path):
        path = Path(path)
        key = str(path.resolve())
        if key not in self._hashes:
            self._hashes[key] = (path.stat().st_size, digest(path))
        return self._hashes[key]

    def register(self, record):
        name = relative_name(record["destination"])
        record["destination"] = name
        key = name.casefold()
        if key in self.files:
            old = self.files[key]
            if old["sha256"] != record["sha256"] or old["bytes"] != record["bytes"]:
                raise ValueError("Conflicting destination: " + name)
            return
        self.files[key] = record

    def add_file(self, path, destination, category, allowed, expected=None, **origin):
        path = checked_source(path, allowed)
        if not path.is_file():
            self.error("Missing " + category + ": " + str(path))
            return
        size, sha = self.hashed(path)
        if expected and (sha != expected["sha256"] or size != expected.get("bytes", expected.get("size"))):
            raise ValueError("Recorded source hash/size mismatch: " + str(path))
        self.register({"destination": destination, "source": str(path), "origin_root": str(allowed),
                       "category": category, "bytes": size, "sha256": sha, **origin})

    def require_input(self, path, category, allowed):
        path = checked_source(path, allowed)
        if not path.is_file():
            self.error("Missing " + category + ": " + str(path))
            return
        size, sha = self.hashed(path)
        self.inputs.append({"source": str(path), "category": category, "bytes": size, "sha256": sha})

    def generated(self, destination, content, recipe, category="generated_configuration"):
        payload = content.encode("utf-8")
        self.register({"destination": destination, "category": category,
                       "content_utf8": content, "recipe": recipe,
                       "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})

    def resource_tree(self, source, destination, category, manifest=None):
        checked_source(source, source)
        if not source.is_dir():
            self.error("Missing resource tree: " + str(source))
            return
        count = 0
        for path in sorted(source.rglob("*")):
            relative = path.relative_to(source)
            if any(p.startswith(".") for p in relative.parts) or not path.is_file():
                continue
            if path.suffix.lower() in FORBIDDEN_RESOURCE_SUFFIXES:
                raise ValueError("Native/executable file in resource tree: " + str(path))
            name = relative.as_posix()
            expected = manifest.get(name) if manifest is not None else None
            if manifest is not None and expected is None:
                raise ValueError("Resource absent from build manifest: " + str(path))
            self.add_file(path, destination + "/" + name, category, source, expected,
                          resource_build_origin=expected.get("origin") if expected else None)
            count += 1
        if not count or (manifest is not None and count != len(manifest)):
            self.error("Resource count differs from complete build manifest: " + str(source))


def source_revisions(plan):
    records = read_json(AREA / "worktrees.json")
    env = {**os.environ, "GIT_OPTIONAL_LOCKS": "0", "GIT_NO_LAZY_FETCH": "1"}
    revisions = {}
    for item in records:
        repo = SOURCE if item["name"] == "DesktopEditors" else SOURCE / item["name"]
        checked_source(repo, SOURCE)
        actual = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], env=env, text=True).strip()
        if actual != item["commit"]:
            raise ValueError("Source commit differs from isolated-worktree receipt: " + str(repo))
        revisions[item["name"]] = actual
    plan.provenance["source_commits"] = revisions


def native_outputs(plan, include_host):
    for name in (*CORE_DLLS, "hunspell", "ascdocumentscore"):
        plan.add_file(LIB / (name + ".dll"), name + ".dll", "source_built_native_core", LIB)
        plan.require_input(LIB / (name + ".lib"), "source_build_import_library", LIB)
        if name in CORE_DLLS:
            plan.add_file(LIB / (name + ".dll"), "converter/" + name + ".dll", "source_built_native_core", LIB)
    for name in STATIC_LIBS:
        plan.require_input(LIB / (name + ".lib"), "source_build_static_library", LIB)
    for name in ("x2t", "allfontsgen", "allthemesgen", "docbuilder", "pluginsmanager", "ooxml_crypt"):
        plan.add_file(BIN / (name + ".exe"), "converter/" + name + ".exe", "source_built_native_tool", BIN)
    plan.add_file(BIN / "editors_helper.exe", "editors_helper.exe", "source_built_desktop_helper", BIN)
    for name in ("openoffice_qt6_documentscore", "openoffice_qt6_videoplayer"):
        plan.add_file(WRAPPERS / "bin" / (name + ".dll"), name + ".dll", "source_built_qt_wrapper", WRAPPERS)
        plan.require_input(WRAPPERS / "lib" / (name + ".lib"), "source_build_wrapper_import", WRAPPERS)
    cache = WRAPPERS / "CMakeCache.txt"
    if not cache.is_file():
        plan.error("Missing wrapper CMakeCache: " + str(cache))
    else:
        values = cache_values(cache)
        for key, expected in {"ONLYOFFICE_SOURCE_ROOT": SOURCE, "ONLYOFFICE_NATIVE_IMPORT_DIR": LIB}.items():
            if Path(values.get(key, "")).resolve() != expected.resolve():
                plan.error("Wrapper cache does not use isolated input: " + key)
        plan.require_input(cache, "wrapper_build_configuration", WRAPPERS)
    if include_host:
        plan.add_file(HOST / "bin/OpenOffice.exe", "OpenOffice.exe", "locally_built_host", HOST)
        cache = HOST / "CMakeCache.txt"
        if not cache.is_file():
            plan.error("Missing new host CMakeCache: " + str(cache))
        else:
            values = cache_values(cache)
            paths = {"ONLYOFFICE_SOURCE_ROOT": SOURCE, "ONLYOFFICE_RUNTIME_ROOT": TARGET,
                     "ONLYOFFICE_CORE_IMPORT_LIBRARY": LIB / "ascdocumentscore.lib",
                     "ONLYOFFICE_QT_WRAPPER_BUILD": WRAPPERS}
            for key, expected in paths.items():
                if Path(values.get(key, "")).resolve() != expected.resolve():
                    plan.error("Host cache does not use isolated input: " + key)
            if values.get("OPEN_OFFICE_WITH_ONLYOFFICE") != "ON" or values.get("CMAKE_BUILD_TYPE") != "Release":
                plan.error("Host must be a Release build with the real ONLYOFFICE adapter")
            plan.require_input(cache, "host_build_configuration", HOST)


def third_party(plan):
    icu = CORE / "Common/3dParty/icu/win_64/build"
    for name in ("icuuc74.dll", "icudt74.dll"):
        for destination in (name, "converter/" + name):
            plan.add_file(icu / name, destination, "source_built_icu", icu)
    v8 = CORE / "Common/3dParty/v8_89/v8"
    v8_output = v8 / "out.gn/win_64/release"
    plan.require_input(v8_output / "obj/v8_monolith.lib", "source_built_v8_monolith", v8)
    plan.add_file(v8_output / "icudtl.dat", "converter/icudtl.dat", "v8_pinned_icu_data", v8,
                  expected={"bytes": 10413552, "sha256": "c5f9a62ecc982a52777c96d96aee791373f0a132c4ec291891d76ddcf2ec2fa9"})
    extra = v8_output / "icudtl_extra.dat"
    if extra.is_file():
        plan.add_file(extra, "converter/icudtl_extra.dat", "v8_pinned_icu_data", v8,
                      expected={"bytes": 1801520, "sha256": "51e596a4aca472588a7bcb233aefcab8ebc5867cf07cd530862c1a6a5d8c708f"})
    if not CEF_RECEIPT.is_file():
        plan.error("Missing verified CEF stage receipt: " + str(CEF_RECEIPT))
        return
    receipt = read_json(CEF_RECEIPT)
    if receipt.get("archive_sha256") != CEF_SHA256 or digest(CEF_ARCHIVE) != CEF_SHA256:
        raise ValueError("CEF official cache hash mismatch")
    if Path(receipt.get("destination", "")).resolve() != CEF.resolve():
        raise ValueError("CEF stage receipt points to another directory")
    seen = set()
    for item in receipt["files"]:
        name = relative_name(item["path"])
        if name in {"cef_sandbox.lib", "libcef.lib"}:
            continue
        if name not in CEF_ROOT_FILES and not (name.startswith("locales/") and name.endswith(".pak")):
            raise ValueError("Unexpected CEF payload: " + name)
        member = item["archive_member"]
        if member not in {"cef_binary/Release/" + name, "cef_binary/Resources/" + name}:
            raise ValueError("CEF member does not match its runtime destination: " + member)
        plan.add_file(CEF / name, name, "official_prebuilt_cef", CEF, item,
                      archive=str(CEF_ARCHIVE), archive_sha256=CEF_SHA256, archive_member=member)
        seen.add(name)
    for missing in sorted((CEF_ROOT_FILES | {"locales/en-US.pak", "locales/zh-CN.pak"}) - seen):
        plan.error("Missing CEF runtime payload: " + missing)
    plan.provenance["cef_receipt"] = {"path": str(CEF_RECEIPT), "sha256": digest(CEF_RECEIPT)}


def qt_files(plan):
    if not QT_SIMULATION.is_dir():
        raise FileNotFoundError("Existing Qt simulation evidence directory is missing: " + str(QT_SIMULATION))
    inputs = [QT / "bin" / ("Qt6" + module + ".dll") for module in QT_MODULES]
    for path in inputs:
        plan.add_file(path, path.name, "installed_qt_6_11_1", QT)
    command = [str(QT / "bin/windeployqt.exe"), "--dry-run", "--json", "--release",
               "--no-patchqt", "--no-compiler-runtime", "--no-translations",
               "--no-system-d3d-compiler", "--no-system-dxc-compiler", "--force", "--dir", str(QT_SIMULATION),
               "--plugindir", str(QT_SIMULATION), *map(str, inputs)]
    env = {**os.environ, "PATH": str(QT / "bin") + os.pathsep + os.environ.get("PATH", "")}
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, timeout=120)
    if result.returncode:
        raise RuntimeError("Qt deployment inventory failed: " + result.stderr[-3000:])
    data = json.loads(result.stdout[result.stdout.index("{"):])
    for item in data["files"]:
        path = Path(item["source"])
        checked_source(path, QT)
        relative = path.relative_to(QT)
        if relative.parts[0].lower() == "bin":
            name = path.name
        elif relative.parts[0].lower() == "plugins":
            name = "qt6-plugins/" + Path(*relative.parts[1:]).as_posix()
        else:
            raise ValueError("Unexpected Qt deployment source layout: " + str(path))
        plan.add_file(path, name, "installed_qt_6_11_1", QT)
    plan.provenance["qt_inventory"] = {"command": command, "stderr": result.stderr,
                                      "copies_performed_by_windeployqt": False, "patch_qt": False,
                                      "simulation_directory": str(QT_SIMULATION),
                                      "simulation_note": "Qt dry-run can create top-level output directories; both specified directories already exist"}


def resources(plan):
    sdk_result = read_json(ROOT / "evidence/full-source-js-sdk-result.json")
    web_result = read_json(ROOT / "evidence/full-source-web-build-result.json")
    if sdk_result.get("status") != "passed" or web_result.get("status") != "passed":
        raise ValueError("Editor resource builds have not passed")
    sdk_manifest = read_json(ROOT / "evidence/full-source-js-sdk-manifest.json")
    web_manifest = read_json(ROOT / "evidence/full-source-web-build-manifest.json")
    plan.resource_tree(SOURCE / "sdkjs/deploy/sdkjs", "editors/sdkjs", "source_sdkjs_deploy",
                       {item["path"]: item for item in sdk_manifest["files"]})
    plan.resource_tree(SOURCE / "web-apps/deploy/web-apps", "editors/web-apps", "source_web_apps_deploy",
                       {item["path"]: item for item in web_manifest})
    plan.resource_tree(SOURCE / "desktop-sdk/ChromiumBasedEditors/resources/local",
                       "editors/sdkjs/common/Images/local", "pinned_desktop_sdk_resources")
    plan.resource_tree(SOURCE / "dictionaries", "dictionaries", "pinned_dictionary_sources")
    if digest(RESOURCE_ARCHIVE) != RESOURCE_SHA256:
        raise ValueError("Official resource-only archive hash mismatch")
    selected = Counter()
    with zipfile.ZipFile(RESOURCE_ARCHIVE) as archive:
        seen = set()
        for item in archive.infolist():
            if item.is_dir():
                continue
            name = relative_name(item.filename)
            if stat.S_ISLNK((item.external_attr >> 16) & 0xffff):
                raise ValueError("Symlink in resource archive: " + name)
            if name.casefold() in seen:
                raise ValueError("Duplicate archive destination: " + name)
            seen.add(name.casefold())
            if not resource_member_allowed(name):
                continue
            with archive.open(item) as stream:
                sha = hashlib.file_digest(stream, "sha256").hexdigest()
            plan.register({"destination": name, "category": "official_release_resource_only",
                           "archive": str(RESOURCE_ARCHIVE), "archive_sha256": RESOURCE_SHA256,
                           "archive_member": item.filename, "bytes": item.file_size, "sha256": sha})
            selected[name.split("/")[0]] += 1
    if selected != {"fonts": 21, "converter": 192, "editors": 1}:
        raise ValueError("Pinned resource-only inventory changed: " + str(selected))
    plan.provenance["resource_build_limits"] = {
        "sdkjs_minified": False, "sdkjs_wasm_rebuilt": False, "web_raster_recompression": False,
        "forms_addon": False, "plugins_enabled": False,
        "font_index": "Generated by host CheckFonts into its fresh profile; never copied from fallback",
        "official_archive_scope": "21 fonts/licenses, 192 empty templates, noconnect.html only",
    }
    plan.generated("converter/DoctRenderer.config", DOCT_CONFIG,
                   "Pinned build_tools/scripts/base.py generate_doctrenderer_config desktop mapping")
    plan.generated("qt6.conf", "[Paths]\nPrefix=.\nPlugins=qt6-plugins\nTranslations=translations\n",
                   "Project Qt6 plugin directory; unmodified installed Qt DLL bytes")
    plan.generated("OPEN_OFFICE_THIRD_PARTY_NOTICES.md", STAGE_NOTICES,
                   "Source-stage attribution and origin description; not the old prebuilt-stage status",
                   category="generated_source_stage_notices")
    for name in ("DesktopEditors", "desktop-sdk", "core"):
        source = SOURCE if name == "DesktopEditors" else SOURCE / name
        plan.add_file(source / "LICENSE", "OPEN_OFFICE_SOURCE_LICENSES/ONLYOFFICE-" + name + "-LICENSE",
                      "pinned_source_license", source)


def pe_imports(path):
    """Inspect x64 PE imports without loading any DLL or executing its code."""
    payload = path.read_bytes()
    if payload[:2] != b"MZ":
        raise ValueError("Native output is not a PE image: " + str(path))
    pe = struct.unpack_from("<I", payload, 0x3c)[0]
    if payload[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", payload, pe + 4)[0] != 0x8664:
        raise ValueError("Native output is not Windows x64: " + str(path))
    sections, optional_size = struct.unpack_from("<H", payload, pe + 6)[0], struct.unpack_from("<H", payload, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", payload, optional)[0] != 0x20b:
        raise ValueError("Expected PE32+ optional header: " + str(path))
    section_table = optional + optional_size

    def offset(rva):
        for index in range(sections):
            virtual_size, address, raw_size, raw = struct.unpack_from("<4I", payload, section_table + index * 40 + 8)
            if address <= rva < address + max(virtual_size, raw_size):
                return raw + rva - address
        raise ValueError("PE RVA outside sections: " + str(path))

    rva = struct.unpack_from("<I", payload, optional + 112 + 8)[0]
    if not rva:
        return []
    current, names = offset(rva), []
    for _ in range(4096):
        record = struct.unpack_from("<5I", payload, current)
        if not any(record):
            return names
        start = offset(record[3])
        end = payload.index(b"\0", start)
        names.append(payload[start:end].decode("ascii"))
        current += 20
    raise ValueError("Unterminated PE imports: " + str(path))


def check_imports(plan):
    system = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"
    checked = {}
    for record in list(plan.files.values()):
        if "source" not in record or Path(record["destination"]).suffix.lower() not in {".dll", ".exe"}:
            continue
        source = record["source"]
        if source not in checked:
            checked[source] = pe_imports(Path(source))
        destination = PurePosixPath(record["destination"])
        for name in checked[source]:
            if "/" in name or "\\" in name or ":" in name:
                raise ValueError("Unexpected DLL import name: " + name)
            # The converter is a separate process; its loader does not inherit
            # the host's DLL-directory registrations or search the parent folder.
            candidates = {(destination.parent / name).as_posix().casefold()}
            if not destination.as_posix().startswith("converter/"):
                candidates.add(name.casefold())
            if any(candidate in plan.files for candidate in candidates):
                continue
            if name.lower().startswith(("api-ms-win-", "ext-ms-win-")) or (system / name).is_file():
                continue
            plan.error("Unresolved native import: " + record["destination"] + " -> " + name)
        record["pe_imports"] = checked[source]


def build_inventory(include_host):
    plan = Inventory()
    for action in (source_revisions, lambda p: native_outputs(p, include_host), third_party, qt_files, resources, check_imports):
        print("INVENTORY_PHASE " + action.__name__, flush=True)
        try:
            action(plan)
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError, struct.error) as error:
            plan.error(type(error).__name__ + ": " + str(error))
    return plan


def copy_record(record, destination):
    target = destination / relative_name(record["destination"])
    if not target.resolve().is_relative_to(destination.resolve()):
        raise ValueError("Runtime target escaped staging directory")
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as output:
        if "source" in record:
            source = checked_source(record["source"], record["origin_root"])
            with source.open("rb") as stream:
                while chunk := stream.read(1024 * 1024):
                    output.write(chunk)
        elif "archive_member" in record:
            if record["archive"] != str(RESOURCE_ARCHIVE) or not resource_member_allowed(record["archive_member"]):
                raise ValueError("Unapproved archive extraction")
            with zipfile.ZipFile(RESOURCE_ARCHIVE) as archive, archive.open(record["archive_member"]) as stream:
                while chunk := stream.read(1024 * 1024):
                    output.write(chunk)
        else:
            output.write(record["content_utf8"].encode("utf-8"))
    if target.stat().st_size != record["bytes"] or digest(target) != record["sha256"]:
        raise ValueError("Source changed during staging: " + record["destination"])


def write_json(path, data, exclusive=True):
    with path.open("x" if exclusive else "w", encoding="utf-8", newline="\n") as stream:
        json.dump(data, stream, indent=2, ensure_ascii=False)
        stream.write("\n")


def stage(report, finish_host):
    if TARGET.absolute() != TARGET.resolve() or TARGET.parent.resolve() != (ROOT / "build").resolve():
        raise ValueError("Runtime target is redirected or outside build")
    files = {item["destination"].casefold(): item for item in report["files"]}
    if finish_host:
        previous = read_json(TARGET / MARKER_NAME)
        if previous.get("application_state") != "engine-only/incomplete-app" or previous.get("target") != str(TARGET):
            raise ValueError("Only this script's engine-only stage can be finished")
        old = {item["destination"].casefold(): item for item in previous["files"]}
        actual = {p.relative_to(TARGET).as_posix().casefold() for p in TARGET.rglob("*") if p.is_file()}
        allowed_existing = set(old) | {MARKER_NAME.casefold(), "openoffice.exe"}
        if not ((set(old) | {MARKER_NAME.casefold()}) <= actual) or not actual <= allowed_existing:
            raise ValueError("Engine-only stage has added or missing files; inspect before finishing")
        for name, item in old.items():
            target = checked_source(TARGET / item["destination"], TARGET)
            if name not in files or files[name]["sha256"] != item["sha256"] or digest(target) != item["sha256"]:
                raise ValueError("Engine-only stage or source changed: " + item["destination"])
        additions = [item for name, item in files.items() if name not in old]
        if {item["destination"] for item in additions} != {"OpenOffice.exe"}:
            raise ValueError("Finishing is limited to the newly built host")
        host = TARGET / "OpenOffice.exe"
        if host.exists():
            checked_source(host, TARGET)
            if host.stat().st_size != additions[0]["bytes"] or digest(host) != additions[0]["sha256"]:
                raise ValueError("Existing intermediate host does not match this build")
        else:
            pending = TARGET.parent / (".full-source-host.staging-" + uuid.uuid4().hex)
            pending.mkdir()
            report["pending_directory"] = str(pending)
            copy_record(additions[0], pending)
            (pending / "OpenOffice.exe").rename(host)
            report["retained_host_transaction_directory"] = str(pending)
            report.pop("pending_directory")
        committed = {**report, "staged": True, "status": "staged_pending_runtime_validation"}
        temporary = TARGET.parent / (".full-source-marker-" + uuid.uuid4().hex + ".json")
        write_json(temporary, committed)
        temporary.replace(TARGET / MARKER_NAME)
        report.update(staged=True, status="staged_pending_runtime_validation")
        return
    if TARGET.exists():
        raise FileExistsError("New runtime already exists; no overwrite is allowed: " + str(TARGET))
    pending = TARGET.parent / (".full-source-run.staging-" + uuid.uuid4().hex)
    pending.mkdir()
    # A failed copy is retained for inspection; never delete or overwrite a runtime.
    report["pending_directory"] = str(pending)
    for item in report["files"]:
        copy_record(item, pending)
    committed = {**report, "staged": True, "status": "staged_pending_runtime_validation"}
    committed.pop("pending_directory")
    write_json(pending / MARKER_NAME, committed)
    pending.rename(TARGET)
    report.pop("pending_directory")
    report.update(staged=True, status="staged_pending_runtime_validation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--engine-only", action="store_true", help="Create the complete engine stage, intentionally without OpenOffice.exe")
    mode.add_argument("--finish-host", action="store_true", help="Verify the engine-only stage and add the new host")
    parser.add_argument("--dry-run", action="store_true", help="Inventory only, including when combined with an explicit mode")
    parser.add_argument("--report", type=Path, help="New JSON file inside project evidence; never overwritten")
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_path = args.report or ROOT / ("evidence/full-source-runtime-inventory-" + stamp + ".json")
    if not report_path.is_absolute():
        report_path = ROOT / report_path
    checked_source(report_path, ROOT / "evidence")
    if report_path.exists():
        parser.error("Report already exists; choose a new --report filename")
    include_host = args.finish_host or not args.engine_only
    plan = build_inventory(include_host)
    report = {"schema_version": 1, "created_at_utc": datetime.now(timezone.utc).isoformat(),
              "staging_script_sha256": digest(Path(__file__)),
              "target": str(TARGET), "application_state": "built-host/pending-runtime-validation" if include_host else "engine-only/incomplete-app",
              "status": "blocked_missing_or_invalid_inputs" if plan.errors else "inventory_verified",
              "staged": False, "docx_roundtrip_verified": False,
              "compiler_runtime": "Existing installed Microsoft VC runtime; no installer or system configuration changes",
              "errors": plan.errors, "provenance": plan.provenance,
              "build_inputs": plan.inputs, "files": sorted(plan.files.values(), key=lambda item: item["destination"].casefold())}
    if not plan.errors and not args.dry_run and (args.engine_only or args.finish_host):
        try:
            stage(report, args.finish_host)
            report["status"] = "staged_pending_runtime_validation"
        except (OSError, ValueError, KeyError) as error:
            report["errors"].append(type(error).__name__ + ": " + str(error))
            report["status"] = "stage_failed_no_completion_claim"
            report["staged"] = False
    report_path.parent.mkdir(parents=True, exist_ok=True)
    write_json(report_path, report)
    print(json.dumps({"status": report["status"], "staged": report["staged"],
                      "application_state": report["application_state"], "file_count": len(report["files"]),
                      "error_count": len(report["errors"]), "report": str(report_path),
                      "first_errors": report["errors"][:8]}, indent=2))
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
