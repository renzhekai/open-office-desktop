#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prepare the isolated WASM source tree (build/wasm-source/core) for one module.

Re-implements, with verifiable steps, what the historical ONLYOFFICE
run_before scripts did, instead of running them (they edit shared trees in
place, can delete dependencies, and the OpenSSL one refuses Windows):

* add historical wrapper/recipe files from the 075052dd sparse checkout, only
  where the pinned core has no file at that path (never overwrite);
* copy fetched 3dParty sources from the native full-source tree after checking
  their commit and that local modifications are exactly the expected ones;
* copy FreeType into graphics/pro/js and apply the historical wasm patches;
* unpack the SHA256-verified OpenSSL 1.1.1f release and generate its config
  headers with OpenSSL's own Configure (approved portable Perl);
* apply the remaining historical one-line edits.

Every replacement must match exactly once. Steps already done by an earlier
run are verified and reported as already present, so modules can be prepared
one after another in the same tree. Nothing in build/full-source, upstream or
the accepted runtime directories is modified.

Usage:
    python -I scripts/prepare_wasm_module_source.py --module <fonts|zlib|spell|hash> --label <new-label>
"""

import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "build" / "wasm-source" / "core"
HISTORY = ROOT / "build" / "wasm-source" / "history-075052dd"
PREPARED = ROOT / "build" / "full-source" / "DesktopEditors" / "core"
PERL = ROOT / "build" / "full-source" / "tools" / "strawberry-perl-5.40.0.1" / "perl" / "bin" / "perl.exe"
# OpenSSL's Configure refuses native Windows Perl for Unix targets ("doesn't produce Unix like paths",
# hash pass1). Git for Windows already ships an MSYS Perl that does; it is used only for this step.
UNIX_PERL = Path("C:/Program Files/Git/usr/bin/perl.exe")
# That Perl lacks Pod::Usage, which configdata.pm `use`s only for its --help/--man mode; a stand-in
# that dies if actually called satisfies the compile-time `use` (hash pass2).
PERL_SHIM = ROOT / "build" / "wasm-source" / "toolchain" / "perl-shim"


def msys_path(path):
    """D:/x -> /d/x, for PERL5LIB under MSYS Perl (a ':' would split the list)."""
    posix = Path(path).as_posix()
    return "/" + posix[0].lower() + posix[2:] if len(posix) > 1 and posix[1] == ":" else posix


def unix_perl_env():
    # Configure re-runs Perl on configdata.pm in a child process, so -I is not enough (hash pass3).
    env = dict(os.environ)
    env["PERL5LIB"] = msys_path(PERL_SHIM)
    return env
OPENSSL_TARBALL = ROOT / "dependencies" / "full-source" / "openssl-1.1.1f.tar.gz"
OPENSSL_SHA256 = "186c6bfe6ecfba7a5b48c47f8a1673d0f3b0e5ba2e25602dd23b629975da3f35"
CORE_COMMIT = "ed63e94de814ff1d4ea28cddfaba6939bc9df9ed"
HISTORY_COMMIT = "075052dd3f41373affa533317d6734a0d34e0897"

GRAPHICS_JS = "DesktopEditor/graphics/pro/js"
PATCHES = GRAPHICS_JS + "/wasm/patches"
FREETYPE_SRC = "DesktopEditor/freetype-2.10.4"
FREETYPE_DST = GRAPHICS_JS + "/freetype-2.10.4"
HUNSPELL = "Common/3dParty/hunspell/hunspell"
XMLSEC_WASM = "DesktopEditor/xmlsec/src/wasm"
OPENSSL_DST = XMLSEC_WASM + "/3rdParty/openssl"

# Historical graphics/pro/js/before.py FreeType steps: (copy source -> target or None, patches).
FREETYPE_STEPS = [
    ("src/smooth/ftgrays.c", "src/smooth/ftgrays.cpp", ["ftgrays1", "ftgrays2", "ftgrays3"]),
    ("src/smooth/smooth.c", "src/smooth/smooth.cpp", ["smooth"]),
    (None, "src/base/ftobjs.c", ["ftobjs1", "ftobjs2"]),
    ("src/sfnt/ttcmap.c", "src/sfnt/ttcmap.cpp", ["ttcmap"]),
    ("src/sfnt/sfnt.c", "src/sfnt/sfnt.cpp", ["sfnt"]),
    (None, "builds/unix/ftsystem.c", ["ftsystem"]),
]

# Historical Common/3dParty/hunspell/before.py get_hunspell() edits, in order.
_TL_DEFAULT = ("#define TIMELIMIT_GLOBAL (CLOCKS_PER_SEC / 4)\n#define TIMELIMIT_SUGGESTION (CLOCKS_PER_SEC / 10)\n"
               "#define TIMELIMIT (CLOCKS_PER_SEC / 20)\n")
_TL_CUSTOM = ("#define TIMELIMIT_GLOBAL CUSTOM_TIMELIMIT_GLOBAL\n#define TIMELIMIT_SUGGESTION CUSTOM_TIMELIMIT_SUGGESTION\n"
              "#define TIMELIMIT CUSTOM_TIMELIMIT\n")
HUNSPELL_EDITS = [
    ("src/hunspell/filemgr.hxx", "FileMgr& operator=(const FileMgr&);",
     "FileMgr& operator=(const FileMgr&);\n#ifdef HUNSPELL_WASM_MODULE\nstring_buffer_stream memin;\n#endif"),
    ("src/hunspell/filemgr.hxx", "#include <fstream>",
     "#include <fstream>\n#ifdef HUNSPELL_WASM_MODULE\n#include \"string_buffer_stream.h\"\n#endif\n"),
    ("src/hunspell/csutil.cxx", "void free_utf_tbl() {", "void free_utf_tbl() { \n return;\n"),
    ("src/hunspell/atypes.hxx", _TL_DEFAULT,
     "#ifndef CUSTOM_TIMELIMITS\n" + _TL_DEFAULT + "#else\n" + _TL_CUSTOM + "#endif\n"),
]

# OpenSSL: historical openssl.py ran `emconfigure ./config no-shared no-asm no-threads no-dso` and
# `make build_generated` on Linux/macOS. Here the wasm32-appropriate generic 32-bit Unix target is
# configured explicitly and the same .h.in templates are expanded with OpenSSL's util/dofile.pl.
OPENSSL_CONFIGURE = ["Configure", "linux-generic32", "no-shared", "no-asm", "no-threads", "no-dso"]
# The headers `make build_generated` creates are read from the generated Makefile's GENERATED_MANDATORY
# (1.1.1f: include/crypto/bn_conf.h, include/crypto/dso_conf.h, include/openssl/opensslconf.h).
OPENSSL_SENTINEL = "include/openssl/opensslconf.h"


ICU_TARBALL = ROOT / "dependencies" / "full-source" / "icu4c-74_2-src.tgz"
ICU_SHA512 = ("e6c7876c0f3d756f3a6969cad9a8909e535eeaac352f3a721338b9cbd56864bf"
              "7414469d29ec843462997815d2ca9d0dab06d38c37cdd4d8feb28ad04d8781b0")
ICU_COMMON = "Common/3dParty/icu/icu/source/common"
# Historical graphics/pro/js/before.py edit of the shared ICU tree (applied here to the isolated copy).
ICU_EDIT = ("udata.cpp", "\n{\n    UDataMemory tData;", "\n{\n#ifdef BUILDING_WASM_MODULE\nreturn NULL;\n#endif\n    UDataMemory tData;")
LIBXML = "Common/3dParty/libxml/libxml2"
LIBXML_COMMIT = "39b66eb2089f491eacf8e22a7965a235f7f5735e"
_LIBXML_ERR_TARGET = ("void\nxmlGenericErrorDefaultFunc(void *ctx ATTRIBUTE_UNUSED, const char *msg, ...) {\n    va_list args;\n\n"
                      "    if (xmlGenericErrorContext == NULL)\n\txmlGenericErrorContext = (void *) stderr;\n\n"
                      "    va_start(args, msg);\n    vfprintf((FILE *)xmlGenericErrorContext, msg, args);\n    va_end(args);\n}")
# Current core Common/3dParty/libxml/fetch.py edits, in order.
LIBXML_EDITS = [
    ("include/libxml/parserInternals.h", "#define XML_MAX_TEXT_LENGTH 10000000", "#define XML_MAX_TEXT_LENGTH 15000000"),
    ("include/libxml/parserInternals.h", "#define XML_MAX_LOOKUP_LIMIT 10000000", "#define XML_MAX_LOOKUP_LIMIT 15000000"),
    ("globals.c", "static int xmlGetWarningsDefaultValueThrDef = 1;", "static int xmlGetWarningsDefaultValueThrDef = 0;"),
    ("error.c", _LIBXML_ERR_TARGET, "#ifndef XML_ERROR_DISABLE_MODE\n" + _LIBXML_ERR_TARGET +
     "\n#else\nvoid\nxmlGenericErrorDefaultFunc(void *ctx ATTRIBUTE_UNUSED, const char *msg, ...) {\n\t// NONE\n}\n#endif"),
]
PDF_OPENSSL_DST = "Common/3dParty/openssl/openssl"
# Historical graphics/pro/js/before.py edit: PdfFile/lib/xpdf/Decrypt.cc (C++) #includes sha512.c, which
# needs an explicit cast for C++.
# base.replaceInFile replaces every occurrence; OpenSSL 1.1.1f sha512.c has it in 3 #if branches.
PDF_OPENSSL_EDITS = [("crypto/sha/sha512.c", "const SHA_LONG64 *W = in;", "const SHA_LONG64 *W = (const SHA_LONG64*)in;", 3)]

DRAWINGFILE_WRAPPER = GRAPHICS_JS + "/wasm/src/drawingfile.cpp"
DRAWINGFILE_RECONSTRUCTED = GRAPHICS_JS + "/wasm/src/drawingfile_reconstructed.cpp"
_IMAGE_AND_CLEAR_EXPORTS = """WASM_EXPORT void SetImageBinary(char* path, BYTE* data, int size)
{
	NSImages::IImagesMemoryStorage* pStorage = CDrawingFile::GetImagesStorage();
	if (pStorage)
	{
		std::string sPathA(path);
		pStorage->Add(UTF8_TO_U(sPathA), data, size, true);
	}
}
WASM_EXPORT int IsImageBinaryExist(char* path)
{
	NSImages::IImagesMemoryStorage* pStorage = CDrawingFile::GetImagesStorage();
	if (pStorage)
	{
		std::string sPathA(path);
		NSImages::IImageStream* pStream = pStorage->Get(UTF8_TO_U(sPathA));
		if (pStream)
			return 1;
	}
	return 0;
}
WASM_EXPORT void ClearFontsStorage()
{
	NSFonts::IFontsMemoryStorage* pStorage = CDrawingFile::GetFontsStorage();
	if (pStorage)
		pStorage->Clear();
}
WASM_EXPORT void ClearImagesStorage()
{
	NSImages::IImagesMemoryStorage* pStorage = CDrawingFile::GetImagesStorage();
	if (pStorage)
		pStorage->Clear();
}
"""
# Reconstruction of the current export layer from the historical 075052dd drawingfile.cpp.
# Each step: (old, new, reason, reference). The historical file itself is left untouched.
DRAWINGFILE_RECONSTRUCTION = [
    ("\t// always recreate storage\n\tCDrawingFile::InitFontsGlobalStorage();\n",
     "\t// always recreate storage\n\tCDrawingFile::InitFontsGlobalStorage();\n\tCDrawingFile::InitImagesGlobalStorage();\n",
     "WASM PdfWriter reads embedded images from the global images storage; the current CDrawingFile provides "
     "InitImagesGlobalStorage next to InitFontsGlobalStorage; same lifecycle as the fonts storage",
     "DesktopEditor/doctrenderer/drawingfile.h:136; PdfFile/PdfWriter.cpp:141"),
    ("\tNSFonts::NSApplicationFontStream::SetGlobalMemoryStorage(NULL);\n}",
     "\tNSFonts::NSApplicationFontStream::SetGlobalMemoryStorage(NULL);\n"
     "\tNSImages::NSApplicationImageStream::SetGlobalImagesMemoryStorage(NULL);\n}",
     "release the images storage on Close exactly like the fonts storage",
     "DesktopEditor/graphics/pro/pro_Image.cpp SetGlobalImagesMemoryStorage (releases the previous storage)"),
    ("\treturn 0;\n}\n\nWASM_EXPORT CDrawingFile* Open(",
     "\treturn 0;\n}\n" + _IMAGE_AND_CLEAR_EXPORTS + "\nWASM_EXPORT CDrawingFile* Open(",
     "_SetImageBinary(path,data,size) and _IsImageBinaryExist(path) are called without a file by the current "
     "loader; bodies equal the CDrawingFile members, via the static GetImagesStorage like the historical "
     "SetFontBinary/IsFontBinaryExist; _ClearFontsStorage()/_ClearImagesStorage() take no arguments and clear "
     "the global storages through the IMemoryStorage::Clear interface",
     "drawingfile.h:132-142,682-697; graphics/pro/MemoryStorage.h:87; sdkjs loader imageToMemory/clearFontsStorage"),
    ("WASM_EXPORT BYTE* GetStructure(CDrawingFile* pFile)\n{\n\treturn pFile->GetStructure();\n}\n",
     "WASM_EXPORT BYTE* GetStructure(CDrawingFile* pFile)\n{\n\treturn pFile->GetStructure();\n}\n"
     "WASM_EXPORT BYTE* GetMacros(CDrawingFile* pFile)\n{\n\treturn pFile->GetMacros();\n}\n",
     "current loader calls _GetMacros(nativeFile) and reads the returned buffer", "drawingfile.h:453"),
    ("WASM_EXPORT void SetScanPageFonts(CDrawingFile* pFile, int nPageIndex)\n{\n\treturn pFile->SetScanPageFonts(nPageIndex);\n}",
     "WASM_EXPORT BYTE* SetScanPageFonts(CDrawingFile* pFile, int* arrPageIndex, int nLength)\n{\n"
     "\treturn pFile->SetScanPageFonts(arrPageIndex, nLength);\n}",
     "current CDrawingFile::SetScanPageFonts(int*, int) returns BYTE*; the loader passes an Int32 page array "
     "and its length and keeps the returned pointer",
     "drawingfile.h:550; sdkjs loader CFile.prototype._setScanPageFonts"),
    ("WASM_EXPORT int UndoRedact(CDrawingFile* pFile)\n{\n\treturn pFile->UndoRedact() ? 1 : 0;\n}\n",
     "WASM_EXPORT int UndoRedact(CDrawingFile* pFile)\n{\n\treturn pFile->UndoRedact() ? 1 : 0;\n}\n"
     "WASM_EXPORT BYTE* BuildPdf(CDrawingFile* pFile, BYTE* pCommands, int nCommandsLen, const char* password, "
     "BYTE* pMetaData, int nMetaLen)\n{\n"
     "\tstd::wstring sPassword = L\"\";\n\tif (NULL != password)\n"
     "\t\tsPassword = NSFile::CUtf8Converter::GetUnicodeStringFromUTF8((BYTE*)password, strlen(password));\n"
     "\treturn pFile->BuildPdf(pCommands, nCommandsLen, password ? sPassword.c_str() : NULL, pMetaData, nMetaLen);\n}\n",
     "loader calls _BuildPdf(nativeFile, changes, changesLen, utf8 password or 0, meta, metaLen); password "
     "conversion as in the historical Open wrapper and the native DrawingFileEmbed::BuildPdf",
     "drawingfile.h:408; doctrenderer/embed/DrawingFileEmbed.cpp:304-310"),
]


METAFILE_WRAPPER = GRAPHICS_JS + "/wasm/src/metafile.cpp"
METAFILE_RECONSTRUCTED = GRAPHICS_JS + "/wasm/src/metafile_reconstructed.cpp"
_RASTER_STUB = "\t\tvirtual void ConvertToRaster(const wchar_t* wsOutFilePath, unsigned int unFileType, int nWidth, int nHeight = -1) {}\n"
METAFILE_RECONSTRUCTION = [
    (_RASTER_STUB,
     _RASTER_STUB + "\t\tvirtual void ConvertToRaster(BYTE*& pBuffer, int& nSize, unsigned int unFileType, int nWidth, "
                    "int nHeight = -1) { pBuffer = NULL; nSize = 0; }\n",
     "the pinned IMetaFile gained a pure virtual buffer overload of ConvertToRaster (drawingfile pass2 compile error "
     "'allocating an object of abstract class type CMetaFile'); this wrapper is a no-op stub (metafiles are not "
     "supported in the PDF engine), so the overload is a no-op too, returning an empty buffer",
     "DesktopEditor/graphics/pro/Image.h IMetaFile::ConvertToRaster(BYTE*&, int&, unsigned int, int, int)"),
]


def openssl_generated(openssl_dir):
    for line in (openssl_dir / "Makefile").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("GENERATED_MANDATORY="):
            return line.split("=", 1)[1].split()
    raise SystemExit("GENERATED_MANDATORY not found in OpenSSL Makefile")

MODULES = {
    "fonts": {
        "history_dirs": ["Common/js", "DesktopEditor/fontengine/js", GRAPHICS_JS, "OfficeUtils/js",
                         "Common/3dParty/hyphen/js"],
        "third_party": [
            ("Common/3dParty/harfbuzz/harfbuzz", "894a1f72ee93a1fd8dc1d9218cb3fd8f048be29a",
             "core Common/3dParty/harfbuzz/make.py", {"src/hb-ft.cc"}),
            ("Common/3dParty/brotli/brotli", "a47d7475063eb223c87632eed806c0070e70da29",
             "core Common/3dParty/brotli/make.py", set()),
            ("Common/3dParty/hyphen/hyphen", "53820b5d49c64a6e9b8249fcc26d0eb2ed818cbe",
             "not pinned: build_tools core_common/modules/hyphen.py clones default branch; same checkout as native build",
             set()),
        ],
        "freetype": True,
        "edits": [("DesktopEditor/cximage/png/pnglibconf.h",
                   "#define PNG_CONSOLE_IO_SUPPORTED", "//#define PNG_CONSOLE_IO_SUPPORTED")],
    },
    "zlib": {"history_dirs": ["Common/js", GRAPHICS_JS, "OfficeUtils/js"]},
    "spell": {"history_dirs": ["Common/js", "Common/3dParty/hunspell"], "hunspell": True},
    "hash": {"history_dirs": ["Common/js", XMLSEC_WASM], "openssl": True},
    "drawingfile": {"history_dirs": ["Common/js", GRAPHICS_JS], "freetype": True, "icu": True, "libxml": True,
                    "pdf_openssl": True, "drawingfile": True,
                    "edits": [("DesktopEditor/cximage/png/pnglibconf.h",
                               "#define PNG_CONSOLE_IO_SUPPORTED", "//#define PNG_CONSOLE_IO_SUPPORTED")]},
}
KNOWN_TRACKED_EDITS = {rel for module in MODULES.values() for rel, _, _ in module.get("edits", [])}


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                          text=True).stdout


def git_bytes(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True).stdout


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def tree_digest(root, skip_git=True):
    """SHA256 over sorted (relative path, file SHA256) pairs."""
    digest = hashlib.sha256()
    count = 0
    for path in sorted(p for p in root.rglob("*") if p.is_file()
                       and not (skip_git and ".git" in p.relative_to(root).parts)):
        rel = path.relative_to(root).as_posix()
        digest.update(rel.encode("utf-8") + b"\0" + sha256_bytes(path.read_bytes()).encode("ascii") + b"\n")
        count += 1
    return digest.hexdigest(), count


def decode(raw):
    crlf = b"\r\n" in raw
    return raw.decode("utf-8", errors="surrogateescape").replace("\r\n", "\n"), crlf


def encode(text, crlf):
    return (text.replace("\n", "\r\n") if crlf else text).encode("utf-8", errors="surrogateescape")


def replace_once_text(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}: {old[:80]!r}")
    return text.replace(old, new)


def replace_once(path, old, new, record, base=CORE):
    raw = path.read_bytes()
    text, crlf = decode(raw)
    path.write_bytes(encode(replace_once_text(text, old, new, str(path)), crlf))
    record.append({"file": path.relative_to(base).as_posix(), "matches": 1,
                   "sha256_before": sha256_bytes(raw), "sha256_after": sha256_bytes(path.read_bytes())})


def parse_patch(patch_path):
    """Historical Common/js/common.py apply_patch format: <<<<<<< old ======= new >>>>>>>."""
    text, _ = decode(patch_path.read_bytes())
    i1, i2, i3 = text.find("<<<<<<<"), text.find("======="), text.find(">>>>>>>")
    if not (0 <= i1 < i2 < i3):
        raise SystemExit(f"malformed patch {patch_path}")
    return text[i1 + 7:i2].strip(), text[i2 + 7:i3].strip()


def restore_history(dirs, manifest):
    tracked = set(git(CORE, "ls-files").splitlines())
    added, already, kept = [], [], []
    for rel_dir in dirs:
        for src in sorted((HISTORY / rel_dir).rglob("*")):
            if not src.is_file():
                continue
            rel = src.relative_to(HISTORY).as_posix()
            dst = CORE / rel
            if rel in tracked:
                kept.append(rel)
            elif dst.exists():
                if dst.read_bytes() != src.read_bytes():
                    raise SystemExit(f"{rel}: untracked file differs from the historical one")
                already.append(rel)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
                added.append({"path": rel, "sha256": sha256_bytes(dst.read_bytes())})
    manifest["history_files_added"] = added
    manifest["history_files_already_present"] = already
    manifest["history_files_kept_current"] = kept


def check_prepared_repo(rel, commit, allowed):
    src = PREPARED / rel
    head = git(src, "rev-parse", "HEAD").strip()
    if head != commit:
        raise SystemExit(f"{rel}: prepared tree at {head}, expected {commit}")
    changed = {line[3:] for line in git(src, "status", "--porcelain", "--untracked-files=no").splitlines()}
    if changed - allowed:
        raise SystemExit(f"{rel}: unexpected local changes {sorted(changed - allowed)}")
    return src, changed


def copy_tree_checked(rel, src, record):
    dst = CORE / rel
    expected, count = tree_digest(src)
    if dst.exists():
        actual, _ = tree_digest(dst)
        if actual != expected:
            raise SystemExit(f"{dst} exists but differs from {src}")
        record["state"] = "already_present"
    else:
        shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".git"))
        record["state"] = "copied"
    record.update({"files": count, "tree_sha256": expected})


def copy_third_party(entries, manifest):
    copies = []
    for rel, commit, pin, allowed in entries:
        src, changed = check_prepared_repo(rel, commit, allowed)
        record = {"path": rel, "commit": commit, "pin": pin, "modified_files": sorted(changed)}
        copy_tree_checked(rel, src, record)
        copies.append(record)
    manifest.setdefault("third_party_copies", []).extend(copies)


def copy_hunspell(manifest):
    """Copy the native tree's Hunspell after proving its local edits equal the historical before.py edits."""
    commit = (CORE / "Common/3dParty/hunspell/HEAD").read_text(encoding="utf-8").split("\n")[0].strip()
    files = {rel for rel, _, _ in HUNSPELL_EDITS}
    src, changed = check_prepared_repo(HUNSPELL, commit, files)
    verified = []
    for rel in sorted(files):
        text, crlf = decode(git_bytes(src, "show", f"HEAD:{rel}"))
        for edit_rel, old, new in HUNSPELL_EDITS:
            if edit_rel == rel:
                text = replace_once_text(text, old, new, rel)
        actual_text, _ = decode((src / rel).read_bytes())
        if actual_text != text:
            raise SystemExit(f"{rel}: native tree edit differs from historical before.py result")
        verified.append(rel)
    record = {"path": HUNSPELL, "commit": commit, "pin": "core Common/3dParty/hunspell/HEAD",
              "modified_files": sorted(changed), "edits_equal_historical_before_py": verified}
    copy_tree_checked(HUNSPELL, src, record)
    manifest.setdefault("third_party_copies", []).append(record)


def prepare_freetype(manifest):
    src, dst = CORE / FREETYPE_SRC, CORE / FREETYPE_DST
    if dst.exists():
        for _, target, patches in FREETYPE_STEPS:
            text, _ = decode((dst / target).read_bytes())
            for name in patches:
                _, new = parse_patch(CORE / PATCHES / f"{name}.patch")
                if new not in text:
                    raise SystemExit(f"{dst / target} exists without patch {name}")
        manifest["freetype"] = {"state": "already_prepared", "copy": FREETYPE_DST}
        return
    shutil.copytree(src, dst)
    steps = []
    for copy_from, target, patches in FREETYPE_STEPS:
        if copy_from:
            shutil.copy2(dst / copy_from, dst / target)
        applied = []
        for name in patches:
            old, new = parse_patch(CORE / PATCHES / f"{name}.patch")
            replace_once(dst / target, old, new, applied)
        steps.append({"copied_from": copy_from, "target": f"{FREETYPE_DST}/{target}",
                      "patches": patches, "applied": applied})
    manifest["freetype"] = {"state": "prepared", "source": FREETYPE_SRC, "copy": FREETYPE_DST, "steps": steps}


def prepare_openssl(manifest):
    dst = CORE / OPENSSL_DST
    record = {"path": OPENSSL_DST, "tarball": str(OPENSSL_TARBALL), "configure": OPENSSL_CONFIGURE,
              "historical_script": XMLSEC_WASM + "/3rdParty/openssl.py (exits on Windows; not executed)",
              "skipped": {"apps/progs.h copy": "only used by OpenSSL command-line apps, which the hash recipe "
                                              "does not compile; the native tree has no such file"}}
    if dst.exists():
        for rel in openssl_generated(dst):
            if not (dst / rel).is_file():
                raise SystemExit(f"{dst} exists without generated {rel}")
        conf, _ = decode((dst / "include/openssl/opensslconf.h").read_bytes())
        _, new = parse_patch(CORE / XMLSEC_WASM / "3rdParty/patches/openssl2.patch")
        if new not in conf:
            raise SystemExit("opensslconf.h exists without openssl2.patch")
        record["state"] = "already_prepared"
        manifest["openssl"] = record
        return
    digest = sha256_bytes(OPENSSL_TARBALL.read_bytes())
    if digest != OPENSSL_SHA256:
        raise SystemExit(f"OpenSSL tarball SHA256 {digest} != {OPENSSL_SHA256}")
    staging = dst.parent / "openssl-extract"
    if staging.exists():
        raise SystemExit(f"{staging} exists")
    with tarfile.open(OPENSSL_TARBALL) as archive:
        archive.extractall(staging, filter="data")
    (staging / "openssl-1.1.1f").rename(dst)
    staging.rmdir()
    applied = []
    old, new = parse_patch(CORE / XMLSEC_WASM / "3rdParty/patches/openssl1.patch")
    replace_once(dst / "crypto/rand/rand_lib.c", old, new, applied)

    perl_version = subprocess.run([str(UNIX_PERL), "-e", "print qq($^V $^O)"], capture_output=True, text=True).stdout
    configure = subprocess.run([str(UNIX_PERL), *OPENSSL_CONFIGURE], cwd=dst, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", env=unix_perl_env())
    (dst / "configure-openssl-wasm.log").write_text(configure.stdout + configure.stderr, encoding="utf-8")
    if configure.returncode != 0:
        raise SystemExit(f"OpenSSL Configure failed: {configure.stderr[-2000:]}")
    generated = {}
    for rel in openssl_generated(dst):
        out = subprocess.run([str(UNIX_PERL), "-I.", "-Mconfigdata", "util/dofile.pl", "-oMakefile", rel + ".in"],
                             cwd=dst, capture_output=True, env=unix_perl_env())
        if out.returncode != 0:
            raise SystemExit(f"dofile failed for {rel}: {out.stderr[-2000:]!r}")
        (dst / rel).write_bytes(out.stdout)
        generated[rel] = sha256_bytes(out.stdout)
    old, new = parse_patch(CORE / XMLSEC_WASM / "3rdParty/patches/openssl2.patch")
    replace_once(dst / "include/openssl/opensslconf.h", old, new, applied)
    record.update({"state": "prepared", "tarball_sha256": digest,
                   "perl": {"path": str(UNIX_PERL), "version": perl_version,
                            "why": "Git for Windows MSYS Perl (already installed); native Strawberry Perl is "
                                   "rejected by OpenSSL Configure for Unix targets",
                            "pod_usage_shim": {"path": str(PERL_SHIM / "Pod" / "Usage.pm"),
                                               "sha256": sha256_bytes((PERL_SHIM / "Pod" / "Usage.pm").read_bytes())},
                            "failed_attempts": ["build/wasm-source/failed/hash-openssl-prepare-pass1",
                                                "build/wasm-source/failed/hash-openssl-prepare-pass2",
                                                "build/wasm-source/failed/hash-openssl-prepare-pass3",
                                                "build/wasm-source/failed/hash-openssl-prepare-pass4"],
                            "perl5lib": msys_path(PERL_SHIM)},
                   "configure_exit": configure.returncode, "generated_before_patch": generated, "applied": applied})
    manifest["openssl"] = record


def openssl_extract_configure(dst, pre_patches, record):
    """Unpack the verified OpenSSL 1.1.1f release to dst, apply pre_patches, Configure, generate headers."""
    digest = sha256_bytes(OPENSSL_TARBALL.read_bytes())
    if digest != OPENSSL_SHA256:
        raise SystemExit(f"OpenSSL tarball SHA256 {digest} != {OPENSSL_SHA256}")
    staging = dst.parent / "openssl-extract"
    if staging.exists():
        raise SystemExit(f"{staging} exists")
    dst.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(OPENSSL_TARBALL) as archive:
        archive.extractall(staging, filter="data")
    (staging / "openssl-1.1.1f").rename(dst)
    staging.rmdir()
    applied = []
    for rel, old, new in pre_patches:
        replace_once(dst / rel, old, new, applied)
    perl_version = subprocess.run([str(UNIX_PERL), "-e", "print qq($^V $^O)"], capture_output=True, text=True).stdout
    configure = subprocess.run([str(UNIX_PERL), *OPENSSL_CONFIGURE], cwd=dst, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", env=unix_perl_env())
    (dst / "configure-openssl-wasm.log").write_text(configure.stdout + configure.stderr, encoding="utf-8")
    if configure.returncode != 0:
        raise SystemExit(f"OpenSSL Configure failed: {configure.stderr[-2000:]}")
    generated = {}
    for rel in openssl_generated(dst):
        out = subprocess.run([str(UNIX_PERL), "-I.", "-Mconfigdata", "util/dofile.pl", "-oMakefile", rel + ".in"],
                             cwd=dst, capture_output=True, env=unix_perl_env())
        if out.returncode != 0:
            raise SystemExit(f"dofile failed for {rel}: {out.stderr[-2000:]!r}")
        (dst / rel).write_bytes(out.stdout)
        generated[rel] = sha256_bytes(out.stdout)
    record.update({"tarball_sha256": digest, "configure": OPENSSL_CONFIGURE, "configure_exit": configure.returncode,
                   "perl": {"path": str(UNIX_PERL), "version": perl_version, "perl5lib": msys_path(PERL_SHIM)},
                   "generated": generated, "pre_patches": applied})


def prepare_pdf_openssl(manifest):
    dst = CORE / PDF_OPENSSL_DST
    record = {"path": PDF_OPENSSL_DST, "tarball": str(OPENSSL_TARBALL),
              "why": "PdfFile/lib/xpdf/Decrypt.cc #includes crypto/sha/sha512.c and crypto/mem_clr.c from this tree; "
                     "the native tree's copy is configured for VC-WIN64A, so a wasm32 (linux-generic32) copy is made",
              "recipe_include_not_created": "Common/3dParty/openssl/build/linux_64/include (official Linux install "
                                            "dir); the in-tree generated include/openssl/opensslconf.h is used instead",
              "failed_attempt": "build/wasm-source/failed/drawingfile-prepare-pass1 (sha512.c edit expected once, "
                                "found 3; ICU and libxml copies from that run were moved there too)"}
    if dst.exists():
        text, _ = decode((dst / PDF_OPENSSL_EDITS[0][0]).read_bytes())
        if PDF_OPENSSL_EDITS[0][2] not in text or not (dst / OPENSSL_SENTINEL).is_file():
            raise SystemExit(f"{dst} exists but is not prepared")
        record["state"] = "already_prepared"
    else:
        openssl_extract_configure(dst, [], record)
        applied = []
        for rel, old, new, expected in PDF_OPENSSL_EDITS:
            path = dst / rel
            before = path.read_bytes()
            text, text_crlf = decode(before)
            if text.count(old) != expected:
                raise SystemExit(f"{rel}: expected {expected} matches, found {text.count(old)}")
            path.write_bytes(encode(text.replace(old, new), text_crlf))
            applied.append({"file": f"{PDF_OPENSSL_DST}/{rel}", "matches": expected,
                            "sha256_before": sha256_bytes(before), "sha256_after": sha256_bytes(path.read_bytes())})
        record.update({"state": "prepared", "edits": applied})
    manifest["pdf_openssl"] = record


def prepare_icu(manifest):
    root = CORE / "Common/3dParty/icu"
    common = CORE / ICU_COMMON
    record = {"path": ICU_COMMON, "tarball": str(ICU_TARBALL), "version": "74.2",
              "pin": "build_tools scripts/core_common/modules/icu.py icu_major=74 icu_minor=2"}
    if common.exists():
        text, _ = decode((common / ICU_EDIT[0]).read_bytes())
        if ICU_EDIT[2] not in text:
            raise SystemExit(f"{common} exists without the udata.cpp edit")
        record["state"] = "already_prepared"
    else:
        digest = hashlib.sha512(ICU_TARBALL.read_bytes()).hexdigest()
        if digest != ICU_SHA512:
            raise SystemExit(f"ICU tarball SHA512 {digest} != {ICU_SHA512}")
        with tarfile.open(ICU_TARBALL) as archive:
            members = [m for m in archive.getmembers() if m.name.startswith("icu/source/common/")]
            root.mkdir(parents=True, exist_ok=True)
            archive.extractall(root, members=members, filter="data")
        applied = []
        replace_once(common / ICU_EDIT[0], ICU_EDIT[1], ICU_EDIT[2], applied)
        record.update({"state": "prepared", "tarball_sha512": digest, "members": len(members), "edits": applied})
    record["tree_sha256"], record["files"] = tree_digest(common)
    manifest["icu"] = record


def copy_libxml(manifest):
    """Copy the native tree's libxml2 after proving its local edits equal the current fetch.py edits."""
    files = {rel for rel, _, _ in LIBXML_EDITS}
    src, changed = check_prepared_repo(LIBXML, LIBXML_COMMIT, files)
    verified = []
    for rel in sorted(files):
        text, _ = decode(git_bytes(src, "show", f"HEAD:{rel}"))
        for edit_rel, old, new in LIBXML_EDITS:
            if edit_rel == rel:
                text = replace_once_text(text, old, new, rel)
        actual, _ = decode((src / rel).read_bytes())
        if actual != text:
            raise SystemExit(f"{rel}: native libxml2 edit differs from fetch.py result")
        verified.append(rel)
    record = {"path": LIBXML, "commit": LIBXML_COMMIT, "pin": "core Common/3dParty/libxml/fetch.py",
              "modified_files": sorted(changed), "edits_equal_fetch_py": verified,
              "layout_note": "075052dd vendored libxml2 under DesktopEditor/xml/libxml2 with build/qt amalgamations; "
                             "the pinned core fetches it here and builds the sources listed in libxml.pri"}
    copy_tree_checked(LIBXML, src, record)
    manifest.setdefault("third_party_copies", []).append(record)


def reconstruct_wrapper(manifest, key, source_rel, output_rel, reconstruction, what):
    """Write output_rel = historical source_rel + asserted, documented changes (the source stays untouched)."""
    historical = (HISTORY / source_rel).read_bytes()
    if (CORE / source_rel).read_bytes() != historical:
        raise SystemExit(f"restored {source_rel} differs from 075052dd")
    text, crlf = decode(historical)
    steps = []
    for old, new, reason, reference in reconstruction:
        text = replace_once_text(text, old, new, source_rel)
        steps.append({"reason": reason, "reference": reference})
    header = (f"// Reconstructed {what} (Open Office project).\n"
              f"// Generated by scripts/prepare_wasm_module_source.py from ONLYOFFICE core 075052dd\n"
              f"// {source_rel}; every change is listed in the\n"
              f"// drawingfile-prepare evidence with its reason and upstream reference.\n")
    bom = "\ufeff" if text.startswith("\ufeff") else ""
    content = encode(bom + header + text[len(bom):], crlf)
    dst = CORE / output_rel
    if dst.exists():
        if dst.read_bytes() != content:
            raise SystemExit(f"{dst} exists with different content")
        state = "already_present"
    else:
        dst.write_bytes(content)
        state = "written"
    manifest[key] = {"historical": source_rel, "historical_sha256": sha256_bytes(historical),
                     "output": output_rel, "sha256": sha256_bytes(content), "state": state, "steps": steps}


def reconstruct_drawingfile(manifest):
    reconstruct_wrapper(manifest, "drawingfile_reconstruction", DRAWINGFILE_WRAPPER, DRAWINGFILE_RECONSTRUCTED,
                        DRAWINGFILE_RECONSTRUCTION, "export layer for drawingfile.wasm")
    reconstruct_wrapper(manifest, "metafile_reconstruction", METAFILE_WRAPPER, METAFILE_RECONSTRUCTED,
                        METAFILE_RECONSTRUCTION, "metafile stub for drawingfile.wasm")


def apply_edits(edits, manifest):
    applied, already = [], []
    for rel, old, new in edits:
        path = CORE / rel
        text, _ = decode(path.read_bytes())
        if text.count(new) == 1:
            already.append(rel)
        else:
            replace_once(path, old, new, applied)
    manifest["edits"] = applied
    manifest["edits_already_applied"] = already


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--module", choices=sorted(MODULES), required=True)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    config = MODULES[args.module]

    evidence = ROOT / "evidence" / "wasm-source" / f"{args.module}-prepare-{args.label}.json"
    if evidence.exists():
        sys.exit(f"{evidence} exists")
    if git(CORE, "rev-parse", "HEAD").strip() != CORE_COMMIT:
        sys.exit("wasm-source core is not at the pinned commit")
    if git(HISTORY, "rev-parse", "HEAD").strip() != HISTORY_COMMIT:
        sys.exit("history checkout is not at 075052dd")
    tracked_changes = {line[3:] for line in git(CORE, "status", "--porcelain", "--untracked-files=no").splitlines()}
    if tracked_changes - KNOWN_TRACKED_EDITS:
        sys.exit(f"wasm-source core has unexpected tracked changes: {sorted(tracked_changes - KNOWN_TRACKED_EDITS)}")

    manifest = {"module": args.module, "label": args.label,
                "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "core": str(CORE), "core_commit": CORE_COMMIT, "history_commit": HISTORY_COMMIT,
                "historical_scripts_executed": False,
                "tracked_changes_before": sorted(tracked_changes)}
    restore_history(config["history_dirs"], manifest)
    copy_third_party(config.get("third_party", []), manifest)
    if config.get("hunspell"):
        copy_hunspell(manifest)
    if config.get("freetype"):
        prepare_freetype(manifest)
    if config.get("openssl"):
        prepare_openssl(manifest)
    if config.get("icu"):
        prepare_icu(manifest)
    if config.get("libxml"):
        copy_libxml(manifest)
    if config.get("pdf_openssl"):
        prepare_pdf_openssl(manifest)
    if config.get("drawingfile"):
        reconstruct_drawingfile(manifest)
    apply_edits(config.get("edits", []), manifest)
    manifest["tracked_changes_after"] = git(CORE, "status", "--porcelain", "--untracked-files=no").splitlines()
    manifest["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()

    evidence.parent.mkdir(parents=True, exist_ok=True)
    with open(evidence, "x", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"history_added": len(manifest["history_files_added"]),
                      "history_already": len(manifest["history_files_already_present"]),
                      "history_kept_current": len(manifest["history_files_kept_current"]),
                      "third_party": [(c["path"], c["state"], c["files"]) for c in manifest.get("third_party_copies", [])],
                      "freetype": manifest.get("freetype", {}).get("state"),
                      "openssl": manifest.get("openssl", {}).get("state"),
                      "edits": len(manifest["edits"]), "edits_already": manifest["edits_already_applied"],
                      "tracked_changes_after": manifest["tracked_changes_after"],
                      "evidence": str(evidence)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
