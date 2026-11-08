"""Generate an x64 MSVC import library from a verified, never-loaded DLL.

Run only after the containing release archive has passed its independent SHA256
check. This program checks the DLL hash against BOTH --expected-sha256 and the
research JSON, independently parses its PE exports, and refuses unsupported
exports. It does not prove archive provenance, C++ ABI compatibility, or runtime
behaviour. It never loads the DLL and never creates replacement implementations.

Example (PowerShell; substitute an existing absolute path to MSVC lib.exe):
  python generate_import_library.py --dll <verified-dir>/ascdocumentscore.dll \
    --metadata <evidence-dir>/ascdocumentscore.dll.pe-metadata.json \
    --expected-sha256 <independently-verified-DLL-SHA256> \
    --lib-exe <MSVC-bin>/lib.exe --output-dir <new-isolated-output-directory>

The output directory must not exist. Unknown/unnamed exports, forwarders,
ambiguous section mappings, and names unsafe for .def syntax are rejected.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
from datetime import datetime, timezone


class ValidationError(ValueError):
    """An input cannot be used without guessing or losing export information."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_exports(data: bytes) -> dict:
    """Read PE bytes directly; no ctypes, DLL loading, or external inspection."""
    def unpack(fmt: str, offset: int) -> tuple:
        size = struct.calcsize(fmt)
        require(0 <= offset <= len(data) - size, "Truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    def u32(offset: int) -> int:
        return unpack("<I", offset)[0]

    require(data[:2] == b"MZ", "Missing DOS signature")
    pe_offset = u32(0x3C)
    require(pe_offset >= 0x40, "Invalid PE header offset")
    require(data[pe_offset:pe_offset + 4] == b"PE\0\0", "Missing PE signature")
    machine, section_count, _, _, _, optional_size, flags = unpack(
        "<HHIIIHH", pe_offset + 4
    )
    require(machine == 0x8664, "Only IMAGE_FILE_MACHINE_AMD64 (0x8664) is supported")
    require(flags & 0x2000 != 0, "Input PE is not marked as a DLL")
    require(1 <= section_count <= 96, "Unexpected PE section count")
    optional = pe_offset + 24
    require(optional_size >= 120, "PE32+ optional header is too small")
    require(optional + optional_size <= len(data), "Truncated optional header")
    require(unpack("<H", optional)[0] == 0x20B, "Expected PE32+ optional header")
    require(u32(optional + 108) >= 1, "Missing export data directory")
    export_rva, export_size = unpack("<II", optional + 112)
    require(export_rva > 0 and export_size >= 40, "No valid export directory")
    require(export_rva + export_size <= 0x100000000, "Invalid export directory range")

    sections = []
    table = optional + optional_size
    for index in range(section_count):
        offset = table + index * 40
        fields = unpack("<8sIIIIIIHHI", offset)
        try:
            name = fields[0].split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValidationError("Non-ASCII PE section name") from error
        virtual_size, rva, raw_size, raw_offset = fields[1:5]
        require(rva + max(virtual_size, raw_size) <= 0x100000000,
                "Overflowing PE section range")
        require(raw_size == 0 or raw_offset + raw_size <= len(data),
                "Truncated PE section payload")
        sections.append({"name": name, "rva": rva, "virtual_size": virtual_size,
                         "raw_size": raw_size, "raw_offset": raw_offset,
                         "characteristics": fields[-1]})

    def section_at(rva: int) -> dict:
        matches = [s for s in sections if s["rva"] <= rva <
                   s["rva"] + max(s["virtual_size"], s["raw_size"])]
        require(len(matches) == 1, f"RVA 0x{rva:x} has no unique section")
        return matches[0]

    def file_offset(rva: int, size: int) -> int:
        section = section_at(rva)
        delta = rva - section["rva"]
        require(size >= 0 and delta + size <= section["raw_size"],
                f"RVA 0x{rva:x} is not fully backed by file bytes")
        return section["raw_offset"] + delta

    def ascii_at(rva: int) -> str:
        section = section_at(rva)
        offset = file_offset(rva, 1)
        limit = min(section["raw_offset"] + section["raw_size"], offset + 16384)
        end = data.find(b"\0", offset, limit)
        require(end >= 0, "Unterminated or oversized PE export string")
        try:
            return data[offset:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValidationError("Non-ASCII PE export string") from error

    directory = unpack("<IIHHIIIIIII", file_offset(export_rva, 40))
    module_name = ascii_at(directory[4])
    ordinal_base, function_count, name_count, functions, names, ordinals = directory[5:]
    require(0 < function_count <= 100000 and 0 <= name_count <= 100000,
            "Unexpected export table size")
    function_table = file_offset(functions, function_count * 4)
    names_by_index = {}
    if name_count:
        name_table = file_offset(names, name_count * 4)
        ordinal_table = file_offset(ordinals, name_count * 2)
        for index in range(name_count):
            function_index = unpack("<H", ordinal_table + index * 2)[0]
            require(function_index < function_count, "Name references an invalid ordinal")
            require(function_index not in names_by_index,
                    "Multiple names for one ordinal are unsupported; refusing to lose aliases")
            names_by_index[function_index] = ascii_at(u32(name_table + index * 4))

    exports = []
    seen_names = set()
    for index in range(function_count):
        rva = u32(function_table + index * 4)
        if not rva:
            require(index not in names_by_index, "Named export points to an empty slot")
            continue
        ordinal = ordinal_base + index
        require(1 <= ordinal <= 65535, "Export ordinal is outside MSVC .def range")
        name = names_by_index.get(index)
        require(name is not None, f"Ordinal-only export @{ordinal} has no symbol name; refused")
        require(re.fullmatch(r"[A-Za-z_?$@][A-Za-z0-9_?$@.]*", name) is not None,
                f"Export @{ordinal} has an unsupported .def symbol spelling; refused")
        require(name not in seen_names, "Duplicate export name")
        seen_names.add(name)
        require(not export_rva <= rva < export_rva + export_size,
                f"Forwarded export {name!r} is unsupported; no import library generated")
        section = section_at(rva)
        exports.append({"name": name, "ordinal": ordinal, "rva": rva,
                        "forwarder": None, "section": section["name"],
                        "executable_section": bool(section["characteristics"] & 0x20000000)})
    require(bool(exports), "DLL has no supported exports")
    return {"machine": machine, "module_name": module_name,
            "sections": sections, "exports": exports}


def verified_inputs(dll: Path, metadata_path: Path, expected_hash: str) -> tuple:
    require(re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash) is not None,
            "--expected-sha256 must contain exactly 64 hexadecimal digits")
    require(re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*\.dll", dll.name, re.I) is not None,
            "DLL basename is not safe for a .def LIBRARY directive")
    payload = dll.read_bytes()
    actual_hash = digest(payload)
    require(actual_hash == expected_hash.lower(), "DLL SHA256 differs from --expected-sha256")
    metadata_bytes = metadata_path.read_bytes()
    metadata = json.loads(metadata_bytes.decode("utf-8-sig"))
    require(isinstance(metadata, dict), "Metadata must be a JSON object")
    require(metadata.get("sha256") == actual_hash, "DLL SHA256 differs from research JSON")
    require(metadata.get("uncompressed_size") == len(payload), "DLL size differs from research JSON")
    require(isinstance(metadata.get("file"), str) and
            metadata["file"].casefold() == dll.name.casefold(),
            "DLL basename differs from research JSON")
    require(str(metadata.get("machine")).lower() == "0x8664", "Research JSON is not x64")
    actual = parse_exports(payload)
    require(actual["module_name"].casefold() == dll.name.casefold(),
            "PE export module name differs from DLL basename; renaming is not supported")
    require(metadata.get("sections") == actual["sections"],
            "Actual PE sections differ from research JSON")
    require(metadata.get("exports") == actual["exports"],
            "Actual PE export names/ordinals/RVAs/section flags differ from research JSON")
    return actual, actual_hash, digest(metadata_bytes)


def generate(args: argparse.Namespace) -> Path:
    dll = Path(args.dll).resolve(strict=True)
    metadata = Path(args.metadata).resolve(strict=True)
    librarian = Path(args.lib_exe).resolve(strict=True)
    require(dll.is_file() and metadata.is_file(), "DLL and metadata must be regular files")
    require(librarian.is_file() and librarian.name.casefold() == "lib.exe",
            "--lib-exe must identify the actual MSVC lib.exe executable")
    pe, dll_hash, metadata_hash = verified_inputs(dll, metadata, args.expected_sha256)
    output = Path(args.output_dir).resolve()
    require(not output.exists(), "Output directory already exists; refusing any overwrite")
    # mkdir without exist_ok also protects against a competing creation after validation.
    output.mkdir(parents=True, exist_ok=False)
    definition = output / (dll.stem + ".def")
    library = output / (dll.stem + ".lib")
    audit_path = output / (dll.stem + ".import-audit.json")
    lines = [f'LIBRARY "{dll.name}"', "EXPORTS"]
    for export in pe["exports"]:
        suffix = "" if export["executable_section"] else " DATA"
        # Names have already passed a strict .def-safe spelling check. MSVC's
        # librarian preserves quotes around decorated exports as symbol bytes.
        lines.append(f'    {export["name"]} @{export["ordinal"]}{suffix}')
    definition.write_text("\n".join(lines) + "\n", encoding="ascii")
    command = [str(librarian), "/NOLOGO", "/MACHINE:X64",
               f"/DEF:{definition}", f"/OUT:{library}"]
    function_count = sum(e["executable_section"] for e in pe["exports"])
    audit = {
        "schema_version": 1,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "status": "prepared",
        "scope": "Import declarations only; no DLL loading, source stubs, ABI or runtime validation",
        "archive_provenance": "Caller must independently verify the complete release archive before invocation",
        "dll": str(dll), "dll_sha256": dll_hash, "dll_size": dll.stat().st_size,
        "metadata": str(metadata), "metadata_sha256": metadata_hash,
        "machine": "0x8664", "export_module_name": pe["module_name"],
        "classification": "IMAGE_SCN_MEM_EXECUTE on the actual export RVA's unique PE section",
        "function_count": function_count, "data_count": len(pe["exports"]) - function_count,
        "sections": pe["sections"], "exports": pe["exports"],
        "definition": str(definition), "definition_sha256": digest(definition.read_bytes()),
        "library": str(library), "lib_exe_sha256": digest(librarian.read_bytes()),
        "command_argv": command, "shell": False,
    }

    def write_audit() -> None:
        audit_path.write_text(json.dumps(audit, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

    write_audit()
    try:
        result = subprocess.run(command, cwd=output, shell=False, capture_output=True,
                                text=True, errors="replace", timeout=120, check=False)
        audit.update(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr)
        require(result.returncode == 0, f"lib.exe failed with exit code {result.returncode}")
        require(library.is_file() and library.stat().st_size > 8, "lib.exe did not produce an import library")
        library_bytes = library.read_bytes()
        require(library_bytes.startswith(b"!<arch>\n"), "Output is not a COFF library archive")
        inspector = librarian.with_name("dumpbin.exe")
        require(inspector.is_file(), "Matching MSVC dumpbin.exe is required to validate import symbols")
        inspected = subprocess.run([str(inspector), "/NOLOGO", "/LINKERMEMBER:2", str(library)],
                                   shell=False, capture_output=True, text=True,
                                   errors="replace", timeout=60, check=False)
        require(inspected.returncode == 0, "dumpbin could not inspect the generated import archive")
        symbols = set(re.findall(r"^\s*[0-9A-Fa-f]+\s+(\S+)\s*$", inspected.stdout, re.MULTILINE))
        missing = [e["name"] for e in pe["exports"] if "__imp_" + e["name"] not in symbols]
        require(not missing, f"Generated library has missing or altered import symbols: {missing[:3]}")
        (output / (dll.stem + ".import-symbols.log")).write_text(inspected.stdout, encoding="utf-8")
        audit.update(status="generated", library_sha256=digest(library_bytes),
                     library_size=len(library_bytes), verified_import_symbols=len(pe["exports"]))
    except Exception as error:
        audit.update(status="failed", error=f"{type(error).__name__}: {error}")
        raise
    finally:
        write_audit()
    return audit_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dll", required=True, help="Actual DLL extracted from the independently verified release")
    parser.add_argument("--metadata", required=True, help="Matching interface_research PE metadata JSON")
    parser.add_argument("--expected-sha256", required=True, help="Expected SHA256 of the DLL, not the ZIP")
    parser.add_argument("--lib-exe", required=True, help="Absolute path to the selected MSVC x64 lib.exe")
    parser.add_argument("--output-dir", required=True, help="New isolated directory; existing directories are refused")
    args = parser.parse_args()
    try:
        print(f"Import library generated; audit: {generate(args)}")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Import library generation refused/failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
