#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build one SDKJS WebAssembly engine from a historical ONLYOFFICE JSON recipe.

Follows the historical Common/js/make.py flow (per-file emcc -c, partial link
of groups with more than 10 files, final link with EXPORTED_FUNCTIONS, then
splice the generated module into the engine template), but:

* uses the project-local Emscripten 3.1.48 environment (scripts/wasm_toolchain.py);
* never deletes intermediate objects or logs, and keeps failures;
* fails on object name collisions instead of silently overwriting objects;
* asserts template placeholder and post-processing replacement counts;
* minifies with the bundled native Closure compiler instead of Java.

Outputs go to build/wasm-source/out/<name>/<label>; the run record goes to
evidence/wasm-source/<name>-build-<label>.json. Nothing is copied into SDKJS
or any runtime directory.

Usage:
    python -I scripts/build_wasm_module.py --recipe DesktopEditor/fontengine/js/libfont.json --label <new>
"""

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wasm_toolchain  # noqa: E402

ROOT = wasm_toolchain.ROOT
CORE = ROOT / "build" / "wasm-source" / "core"
COMMON_JS = CORE / "Common" / "js"
LICENSE_HEADER = CORE / "Common" / "license" / "header.license"

# Recorded adaptations of a historical recipe to the pinned core tree. Each one must cite the
# official build definition that lists the file, so the recipe follows upstream rather than guesses.
RECIPE_ADAPTATIONS = {
    "fonts": [
        {"group_folder": "../../cximage/CxImage/", "add_files": ["APP14cmyktorgb.cpp"],
         "reason": "ximajpg.cpp in the pinned core calls ima_jpeg_cmyk_to_rgb, defined in the newer "
                   "APP14cmyktorgb.cpp that the 075052dd recipe does not list (wasm-ld undefined symbol, pass1)",
         "official_reference": "DesktopEditor/graphics/pro/raster.pri:268"},
    ],
    "zlib": [
        {"group_folder": "../../DesktopEditor/cximage/CxImage/", "add_files": ["APP14cmyktorgb.cpp"],
         "reason": "same as fonts: ximajpg.cpp calls ima_jpeg_cmyk_to_rgb from APP14cmyktorgb.cpp",
         "official_reference": "DesktopEditor/graphics/pro/raster.pri:268"},
        {"group_folder": "../../DesktopEditor/raster/Metafile/", "add_files": ["Common/CPath.cpp", "Common/CClip.cpp"],
         "reason": "metafile path/clip code moved from Emf/Wmf-specific files into Common/CPath.cpp and Common/CClip.cpp; "
                   "the libfont.json recipe of the same 075052dd snapshot already lists them",
         "official_reference": "DesktopEditor/graphics/pro/metafile.pri:72-73"},
        {"group_folder": "../../DesktopEditor/raster/Metafile/Emf/", "add_files": ["EmfPlusObjects.cpp"],
         "remove_files": ["EmfClip.cpp", "EmfPath.cpp"],
         "reason": "EmfClip.cpp and EmfPath.cpp do not exist at 075052dd or in the pinned core (stale recipe entries); "
                   "EmfPlusObjects.cpp is listed by the official build and by libfont.json",
         "official_reference": "DesktopEditor/graphics/pro/metafile.pri:84"},
        {"group_folder": "../../DesktopEditor/raster/Metafile/Wmf/", "remove_files": ["WmfClip.cpp"],
         "reason": "WmfClip.cpp does not exist at 075052dd or in the pinned core (stale recipe entry)",
         "official_reference": "DesktopEditor/graphics/pro/metafile.pri SOURCES (no WmfClip.cpp)"},
        {"group_folder": "../../DesktopEditor/", "add_files": ["graphics/Matrix.cpp"],
         "reason": "current metafile code uses Aggplus::CMatrix (wasm-ld undefined symbols, zlib pass1); "
                   "libfont.json of the same snapshot already compiles graphics/Matrix.cpp",
         "official_reference": "DesktopEditor/graphics/pro/graphics.pro:77"},
        {"group_folder": "../../DesktopEditor/", "add_files": ["raster/PICT/PICFile.cpp"],
         "reason": "BgraFrame.cpp in the pinned core decodes PICT through CPictFile (wasm-ld undefined symbols, "
                   "zlib pass3); libfont.json of the same snapshot compiles PICT/PICFile.cpp",
         "official_reference": "DesktopEditor/graphics/pro/raster.pri:319"},
        {"group_folder": "../../DesktopEditor/", "add_files": ["graphics/GraphicsPath.cpp"],
         "reason": "current metafile code uses Aggplus::CGraphicsPath (wasm-ld undefined symbols, zlib pass4); "
                   "libfont.json of the same snapshot compiles graphics/GraphicsPath.cpp",
         "official_reference": "DesktopEditor/graphics/pro/graphics.pro:90"},
        {"new_group": {"folder": "../../DesktopEditor/agg-2.4/src/",
                       "files": ["agg_trans_affine.cpp", "agg_bezier_arc.cpp"]},
         "reason": "Matrix.cpp and GraphicsPath.cpp are built on agg::trans_affine and agg::bezier_arc; "
                   "libfont.json compiles the same two files",
         "official_reference": "DesktopEditor/graphics/pro/graphics.pro:80,111"},
        {"add_defines": ["BUILDING_WASM_MODULE"],
         "reason": "the pinned metafile SVG writers skip the font-substitution lookup for WASM builds with "
                   "#ifndef BUILDING_WASM_MODULE; the 2022-era zlib recipe lacks the define, so that code was "
                   "compiled and dereferenced the NULL font manager of MetaFile::Create(NULL) (zlib pass7, "
                   "image10.wmf trap). libfont.json and drawingfile.json of the same snapshot define it. Within "
                   "the zlib sources it also disables CxImage exception handling (ximacfg.h), as in the fonts engine",
         "official_reference": "CInterpretatorSvgBase.cpp:332, CEmfInterpretatorSvg.cpp:1251, cximage/CxImage/ximacfg.h:93; "
                               "libfont.json and drawingfile.json define lists",
         "user_decision": "2026-11-04 user chose to fix the defect before assembling the candidate runtime"},
        {"add_defines": ["IMAGE_CHECKER_DISABLE_XML"],
         "reason": "only the file-path isSvgFile(fileName) uses CXmlLiteReader (wasm-ld undefined symbols, zlib pass2); "
                   "the module is built with FILESYSTEM=0 and detects formats from buffers; the source provides this "
                   "switch with a plain '<svg' scan fallback, and libfont.json of the same snapshot defines it",
         "official_reference": "DesktopEditor/raster/ImageFileFormatChecker.cpp:42,795; libfont.json define list"},
        {"add_flags": ["-s STACK_SIZE=5MB"],
         "reason": "the recipe sets no stack size and the official zlib.wasm (2022-08) was built with the old 5MB default; restored for the same runtime conditions as the official build",
         "official_reference": "emscripten/ChangeLog.md 3.1.27 (2022-11-29): default STACK_SIZE reduced from 5MB "
                               "to 64KB; -sSTACK_SIZE=5MB restores the previous behaviour"},
    ],
    "drawingfile": [
        {"group_folder": "./wasm/src/", "replace_files": {"drawingfile.cpp": "drawingfile_reconstructed.cpp",
                                                          "metafile.cpp": "metafile_reconstructed.cpp"},
         "reason": "the current export layer is not public; drawingfile_reconstructed.cpp is generated by "
                   "prepare_wasm_module_source.py from the 075052dd drawingfile.cpp with 6 recorded changes "
                   "(6 new exports, SetScanPageFonts signature, images storage lifecycle); metafile_reconstructed.cpp "
                   "adds the no-op ConvertToRaster buffer overload required by the pinned IMetaFile (pass2)",
         "official_reference": "evidence/wasm-source/drawingfile-prepare-*.json drawingfile_reconstruction"},
        {"group_folder": "./", "remove_files": ["xml/src/xmllight.cpp", "xml/src/xmldom.cpp",
                                                "xml/build/qt/libxml2_all.c", "xml/build/qt/libxml2_all2.c"],
         "reason": "075052dd copied DesktopEditor/xml (with a vendored libxml2 and build/qt amalgamations) into "
                   "graphics/pro/js/xml; the pinned core keeps xml/src in place and fetches libxml2 separately, "
                   "and xmllight_private.h now includes it by a relative path that a deeper copy would break",
         "official_reference": "DesktopEditor/xml/src/xmllight_private.h:45-47; DesktopEditor/xml/xml.pri"},
        {"new_group": {"folder": "../../../xml/src/", "files": ["xmllight.cpp", "xmldom.cpp"]},
         "reason": "compile the XML wrapper in place (xmlwriter.cpp is already in the common group)",
         "official_reference": "DesktopEditor/xml/xml.pri:41-44"},
        {"new_group": {"folder": "../../../../Common/3dParty/libxml/libxml2/", "files": ['HTMLparser.c', 'HTMLtree.c', 'SAX2.c', 'buf.c', 'c14n.c', 'catalog.c', 'chvalid.c', 'debugXML.c', 'dict.c', 'encoding.c', 'entities.c', 'error.c', 'globals.c', 'hash.c', 'list.c', 'nanohttp.c', 'parser.c', 'parserInternals.c', 'pattern.c', 'relaxng.c', 'schematron.c', 'threads.c', 'tree.c', 'uri.c', 'valid.c', 'xinclude.c', 'xlink.c', 'xmlIO.c', 'xmlmemory.c', 'xmlmodule.c', 'xmlreader.c', 'xmlregexp.c', 'xmlsave.c', 'xmlschemas.c', 'xmlschemastypes.c', 'xmlstring.c', 'xmlwriter.c', 'xpath.c', 'xpointer.c']},
         "reason": "libxml2 sources exactly as the official native build lists them",
         "official_reference": "Common/3dParty/libxml/libxml.pri SOURCES"},
        {"remove_includes": ["../../../xml/libxml2/include", "../../../xml/build/qt"],
         "add_includes": ["../../../../Common/3dParty/libxml", "../../../../Common/3dParty/libxml/include",
                          "../../../../Common/3dParty/libxml/libxml2/include",
                          "../../../../Common/3dParty/libxml/libxml2/include/libxml"],
         "reason": "include paths of the current libxml layout",
         "official_reference": "Common/3dParty/libxml/libxml.pri INCLUDEPATH"},
        {"group_folder": "../../../cximage/CxImage/", "add_files": ["APP14cmyktorgb.cpp"],
         "reason": "same as fonts/zlib: ximajpg.cpp calls ima_jpeg_cmyk_to_rgb (drawingfile pass3 link)",
         "official_reference": "PdfFile/PdfFile.pro:99; DesktopEditor/graphics/pro/raster.pri:268"},
        {"group_folder": "../../../common/", "add_files": ["ProcessEnv.cpp"],
         "reason": "new kernel source after 075052dd (undefined NSProcessEnv::IsPresent/GetBoolValue, pass4 link)",
         "official_reference": "Common/kernel.pro:126"},
        {"group_folder": "../../../../PdfFile/SrcWriter/", "add_files": ["OverlapOutputDev.cpp"],
         "reason": "new PDF writer source after 075052dd (undefined PdfWriter::OverlapOutputDev, pass3 link)",
         "official_reference": "PdfFile/PdfFile.pro SOURCES"},
        {"group_folder": "../../../../PdfFile/SrcReader/", "add_files": ["FontsWasm.cpp"],
         "reason": "WASM-only implementation of NSWasm::IsJSEnv/LoadFont declared in FontsWasm.h and used by "
                   "PdfFont.cpp, RendererOutputDev.cpp and OFDFile (pass3 link); it uses EM_JS, matching the "
                   "__start_em_js/__stop_em_js symbols present in the official drawingfile.js glue",
         "official_reference": "PdfFile/SrcReader/FontsWasm.cpp (no native .pro lists it)"},
        {"new_group": {"folder": "../../../../OFDFile/", "files": ['OFDFile.cpp', 'src/Action.cpp', 'src/Annotation.cpp', 'src/Bookmark.cpp', 'src/Content/ImageObject.cpp', 'src/Content/PageBlock.cpp', 'src/Content/PathObject.cpp', 'src/OFDFile_Private.cpp', 'src/Base.cpp', 'src/Content/Content.cpp', 'src/Content/GraphicUnit.cpp', 'src/Content/Layer.cpp', 'src/Content/TextObject.cpp', 'src/Document.cpp', 'src/OutlineElem.cpp', 'src/Page.cpp', 'src/Res.cpp', 'src/Types/Color.cpp', 'src/Types/ColorSpace.cpp', 'src/Types/CommonData.cpp', 'src/Types/CompositeGraphicUnit.cpp', 'src/Types/DrawParam.cpp', 'src/Types/Font.cpp', 'src/Types/MultiMedia.cpp', 'src/Types/PageArea.cpp', 'src/Types/PenSettings.cpp', 'src/Types/Region.cpp', 'src/Types/Signature.cpp', 'src/Types/TemplatePage.cpp', 'src/Utils/CFontChecker.cpp', 'src/Utils/Types.cpp', 'src/Utils/XmlReader.cpp']},
         "reason": "CDrawingFile in the pinned core opens OFD documents (undefined COFDFile::COFDFile, pass3 link); "
                   "OFDFile sources exactly as the official build lists them",
         "official_reference": "OFDFile/OFDFile.pro SOURCES"},
        {"add_exports": ["_BuildPdf", "_ClearFontsStorage", "_ClearImagesStorage", "_GetMacros",
                         "_IsImageBinaryExist", "_SetImageBinary"],
         "reason": "exports used by the current SDKJS PDF loader and present in the official drawingfile.wasm",
         "official_reference": "evidence/wasm-source/history-abi-check-20261103.json drawingfile exports"},
    ],
    "spell": [
        {"add_flags": ["-s STACK_SIZE=5MB"],
         "reason": "the recipe sets no stack size and the official spell.wasm (2021-05) was built with the old 5MB default; with 64KB Spellchecker_Load overflows the stack (spell pass1 crash, confirmed by the STACK_OVERFLOW_CHECK=2 diagnostic build diag-stackcheck)",
         "official_reference": "emscripten/ChangeLog.md 3.1.27 (2022-11-29): default STACK_SIZE reduced from 5MB "
                               "to 64KB; -sSTACK_SIZE=5MB restores the previous behaviour"},
    ],
}

# Historical run_after scripts, expressed as checked replacements:
# (output file suffix, alternatives, replacement, expected total matches across alternatives).
AFTER = {
    "fonts": [
        ("", ["__ATPOSTRUN__=[];", "__ATPOSTRUN__ = [];"],
         "__ATPOSTRUN__=[function(){window[\"AscFonts\"].onLoadModule();}];", 1),
        ("", ["function getBinaryPromise("], "function getBinaryPromise2(", 1),
        ("_ie", ["__ATPOSTRUN__=[];", "__ATPOSTRUN__ = [];"],
         "__ATPOSTRUN__=[function(){window[\"AscFonts\"].onLoadModule();}];", 1),
        ("_ie", ["function getBinaryPromise("], "function getBinaryPromise2(", 1),
    ],
    # Historical inline run_after replaced "function getBinaryPromise(){" (older Emscripten glue without
    # arguments); Emscripten 3.1.48 emits getBinaryPromise(binaryFile), so the prefix is matched instead,
    # as the fonts after.py already does. Recorded as a post-process adaptation.
    "zlib": [
        ("", ["__ATPOSTRUN__=[];"], "__ATPOSTRUN__=[function(){self.onZlibEngineInit();}];", 1),
        ("", ["function getBinaryPromise("], "function getBinaryPromise2(", 1),
        ("_ie", ["__ATPOSTRUN__=[];"], "__ATPOSTRUN__=[function(){self.onZlibEngineInit();}];", 1),
        ("_ie", ["function getBinaryPromise("], "function getBinaryPromise2(", 1),
    ],
    "spell": [
        ("", ["__ATPOSTRUN__=[];"], "__ATPOSTRUN__=[function(){self.onEngineInit();}];", 1),
        ("", ["function getBinaryPromise("], "function getBinaryPromise2(", 1),
        ("_ie", ["__ATPOSTRUN__=[];"], "__ATPOSTRUN__=[function(){self.onEngineInit();}];", 1),
        ("_ie", ["function getBinaryPromise("], "function getBinaryPromise2(", 1),
    ],
}
AFTER["drawingfile"] = [
    (suffix, alternatives, replacement, expected)
    for suffix in ("", "_ie")
    for alternatives, replacement, expected in (
        (["function getBinaryPromise("], "function getBinaryPromise2(", 1),
        (["__ATPOSTRUN__=[];", "__ATPOSTRUN__ = [];"], "__ATPOSTRUN__=[function(){window[\"AscViewer\"] && window[\"AscViewer\"][\"onLoadModule\"] && window[\"AscViewer\"][\"onLoadModule\"]();}];", 1),
        (['"drawingfile.js.mem"'], 'getMemoryPathIE("drawingfile.js.mem")', None),
    )
]
# drawingfile: the first-party loader template (wasm/js/*.js) was removed from public core in May 2026 and
# the 075052dd copy lacks the current API (BuildPdf, GetMacros, image storage, new SetScanPageFonts). The
# only available current template is inside the official generated loader: take it from there and replace
# only the Emscripten glue, which is rebuilt.
OFFICIAL_TEMPLATES = {
    "drawingfile": {"wasm": ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "pdf" / "src" / "engine" / "drawingfile.js",
                    "asm": ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "pdf" / "src" / "engine" / "drawingfile_ie.js",
                    # first function of the Emscripten glue, and first function of the template suffix (stream.js)
                    "glue_first_function": "locateFile", "template_suffix_first_function": "CBinaryReader",
                    # top-level functions of the 075052dd templates (base, desktop_fetch, stream, drawingfile, wasm)
                    "template_functions": {"getMemoryPathIE", "internal_isLocal", "CBinaryReader", "CBinaryWriter",
                                           "CFile", "readAction", "readAnnot", "readAnnotAP", "readAnnotType",
                                           "readWidgetType", "addToArrayAsDictionary", "fontToMemory", "CWasmPointer"}},
}

# Template text that predates Emscripten 3.1.48 and must change with the glue, with the upstream
# precedent for each change: (old, new, expected matches, reason, reference).
TEMPLATE_ADAPTATIONS = {
    "spell": [
        ("return getBinaryPromise2();", "return getBinaryPromise2.apply(undefined, arguments);", 1,
         "3.1.48 getBinaryPromise2(binaryFile) needs the file name the glue passes to getBinaryPromise; "
         "the old no-argument forwarder would fetch(undefined)",
         "Common/js/desktop_fetch.js:48 (upstream already forwards arguments this way)"),
    ],
}
SDKJS_STRINGSERIALIZE = ROOT / "build" / "full-source" / "DesktopEditors" / "sdkjs" / "common" / "stringserialize.js"
POST_PROCESS_ADAPTATIONS = {
    "zlib": [{"historical": "function getBinaryPromise(){", "used": "function getBinaryPromise(",
              "reason": "Emscripten 3.1.48 glue signature is getBinaryPromise(binaryFile)"},
             {"historical": "copy wasm/js/index.html and wasm/js/code.js into deploy",
              "used": "not copied",
              "reason": "test pages only; the official SDKJS common/zlib/engine deploy has just zlib.js, zlib.wasm, zlib_ie.js"}],
    "spell": [{"historical": "function getBinaryPromise(){", "used": "function getBinaryPromise(",
               "reason": "Emscripten 3.1.48 glue signature is getBinaryPromise(binaryFile)"},
              {"historical": "copy wasm/js/code.js to deploy/spell.js and wasm/js/index.html to deploy",
               "used": "not copied",
               "reason": "test page/client outside the official SDKJS common/spell/spell deploy"},
              {"historical": "asm.js variant with --memory-init-file 0 (embed_mem_file)", "used": "same",
               "reason": "the official 2021 spell_ie.js loads a separate spell.js.mem; the rebuilt one embeds it, "
                         "so deploying it must not keep the stale official spell.js.mem"}],
    "engine": [{"historical": "function getBinaryPromise()", "used": "function getBinaryPromise(",
                "reason": "Emscripten 3.1.48 glue signature is getBinaryPromise(binaryFile)"},
               {"historical": "../../../../../../../sdkjs/common/stringserialize.js",
                "used": "build/full-source/DesktopEditors/sdkjs/common/stringserialize.js",
                "reason": "the pinned SDKJS checkout (9d21388) is the sibling sdkjs tree of the pinned core"}],
}
# Historical after.py minification: Common/js/min.py <file> WHITESPACE_ONLY.
MINIFY = {"fonts": "WHITESPACE_ONLY", "drawingfile": "WHITESPACE_ONLY"}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def split_flags(items):
    flags = []
    for item in items:
        flags.extend(shlex.split(item))
    return flags


def run(cmd, cwd, env):
    result = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return {"cmd": [str(c) for c in cmd], "exit": result.returncode,
            "stdout": result.stdout[-6000:], "stderr": result.stderr[-6000:]}


def variant_flags(recipe, wasm):
    flags = split_flags(recipe["compiler_flags"])
    if wasm:
        flags += ["-s", "WASM=1"]
    else:
        flags += ["-s", "WASM=0", "--closure", "0", "-s", "MIN_IE_VERSION=11"]
        if recipe.get("embed_mem_file"):
            flags += ["--memory-init-file", "0"]
    flags += ["-I" + item for item in recipe["include_path"]]
    flags += ["-D" + item for item in recipe["define"]]
    if not wasm:
        flags += ["-DBUILDING_ASMJS_MODULE", "-D_ARM_ALIGN_"]
    flags += ["-Wno-deprecated-non-prototype", "-Wno-deprecated-register", "-Wno-register",
              "-fvisibility=hidden"]
    return flags


def apply_adaptations(recipe, work):
    applied = []
    for item in RECIPE_ADAPTATIONS.get(recipe["name"], []):
        if "replace_files" in item:
            groups = [g for g in recipe["compile_files_array"] if g.get("folder") == item["group_folder"]]
            if len(groups) != 1:
                raise SystemExit(f"adaptation target group {item['group_folder']} matched {len(groups)} groups")
            for old, new in item["replace_files"].items():
                if old not in groups[0]["files"] or not (work / item["group_folder"] / new).is_file():
                    raise SystemExit(f"cannot replace {old} with {new} in {item['group_folder']}")
                groups[0]["files"][groups[0]["files"].index(old)] = new
            applied.append(item)
            continue
        if "remove_includes" in item or "add_includes" in item:
            for inc in item.get("remove_includes", []):
                if inc not in recipe["include_path"]:
                    raise SystemExit(f"include {inc} not in recipe")
                recipe["include_path"].remove(inc)
            for inc in item.get("add_includes", []):
                if not (work / inc).is_dir():
                    raise SystemExit(f"include dir {inc} does not exist")
                recipe["include_path"].append(inc)
            applied.append(item)
            continue
        if "add_exports" in item:
            for name in item["add_exports"]:
                if name in recipe["exported_functions"]:
                    raise SystemExit(f"{name} already exported")
                recipe["exported_functions"].append(name)
            applied.append(item)
            continue
        if "add_flags" in item:
            recipe["compiler_flags"] = list(recipe["compiler_flags"]) + item["add_flags"]
            applied.append(item)
            continue
        if "add_defines" in item:
            for define in item["add_defines"]:
                if define in recipe["define"]:
                    raise SystemExit(f"define {define} already present")
                recipe["define"].append(define)
            applied.append(item)
            continue
        if "new_group" in item:
            group = item["new_group"]
            if any(g.get("folder") == group["folder"] for g in recipe["compile_files_array"]):
                raise SystemExit(f"group {group['folder']} already exists")
            for name in group["files"]:
                if not (work / group["folder"] / name).is_file():
                    raise SystemExit(f"{name} not found under {group['folder']}")
            recipe["compile_files_array"].append({"folder": group["folder"], "files": list(group["files"])})
            applied.append(item)
            continue
        groups = [g for g in recipe["compile_files_array"] if g.get("folder") == item["group_folder"]]
        if len(groups) != 1:
            raise SystemExit(f"adaptation target group {item['group_folder']} matched {len(groups)} groups")
        files = groups[0]["files"]
        for name in item.get("remove_files", []):
            if name not in files:
                raise SystemExit(f"{name} is not listed in {item['group_folder']}")
            if (work / item["group_folder"] / name).exists():
                raise SystemExit(f"{name} exists in the tree; refusing to drop a real source file")
            files.remove(name)
        for name in item.get("add_files", []):
            if name in files:
                raise SystemExit(f"{name} already listed in {item['group_folder']}")
            if not (work / item["group_folder"] / name).is_file():
                raise SystemExit(f"{name} not found under {item['group_folder']}")
            files.append(name)
        applied.append(item)
    return applied


def build_variant(recipe, work, out, wasm, jobs, env, log):
    name = recipe["name"]
    variant_dir = out / ("wasm" if wasm else "asm")
    arguments = variant_flags(recipe, wasm)
    compile_jobs, link_groups, libs = [], [], []
    for index, group in enumerate(recipe["compile_files_array"]):
        obj_dir = variant_dir / "o" / str(index)
        obj_dir.mkdir(parents=True, exist_ok=True)
        group_args = ["-I" + p for p in group.get("include_path") or []]
        group_args += ["-D" + d for d in group.get("define") or []]
        objects, seen = [], {}
        for item in group["files"]:
            stem = Path(item).stem
            if stem in seen and seen[stem] == item:
                # The same file listed twice (e.g. cryptopp eprecomp.cpp in drawingfile.json): make.py compiled
                # it twice into the same object, which is the same as compiling it once.
                log.append({"duplicate_recipe_entry": item, "group": group["folder"]})
                continue
            if stem in seen:
                raise SystemExit(f"object name collision in group {index}: {seen[stem]} and {item}")
            seen[stem] = item
            obj = obj_dir / f"{stem}.o"
            source = os.path.normpath(os.path.join(group["folder"], item))
            compile_jobs.append((wasm_toolchain.emcc() + ["-o", str(obj), "-c"] + arguments + group_args + [source],
                                 source, obj))
            objects.append(obj)
        if len(group["files"]) > 10:
            combined = obj_dir / f"all_{index}.o"
            link_groups.append((wasm_toolchain.emcc() + ["-o", str(combined), "-r"] + arguments + group_args
                                + [str(o) for o in objects], combined))
            libs.append(combined)
        else:
            libs.extend(objects)

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(run, cmd, work, env): (source, obj) for cmd, source, obj in compile_jobs}
        for future in concurrent.futures.as_completed(futures):
            source, obj = futures[future]
            result = future.result()
            result["source"] = source
            log.append(result)
            if result["exit"] != 0 or not obj.exists():
                failures.append(source)
    summary = {"compiled": len(compile_jobs) - len(failures), "compile_failures": sorted(failures)}
    if failures:
        return summary

    for cmd, combined in link_groups:
        result = run(cmd, work, env)
        log.append(result)
        if result["exit"] != 0:
            summary["partial_link_failure"] = str(combined)
            return summary

    exported = "EXPORTED_FUNCTIONS=[" + ",".join(f"'{f}'" for f in recipe["exported_functions"]) + "]"
    target = variant_dir / f"{name}.js"
    link = wasm_toolchain.emcc() + ["-o", str(target)] + arguments + ["-s", exported]
    link += list(recipe.get("sources") or []) + [str(lib) for lib in libs]
    result = run(link, work, env)
    log.append(result)
    summary["link_exit"] = result["exit"]
    summary["outputs"] = {p.name: {"bytes": p.stat().st_size, "sha256": sha256(p)}
                          for p in sorted(variant_dir.glob(f"{name}.*")) if p.is_file()}
    return summary


def compose(recipe, work, out, wasm, record):
    """Splice the generated module into the engine template (historical make.py finalize)."""
    name = recipe["name"]
    variant_dir = out / ("wasm" if wasm else "asm")
    module_js = (variant_dir / f"{name}.js").read_text(encoding="utf-8")
    content = (official_template(name, wasm, record) if name in OFFICIAL_TEMPLATES
               else (work / recipe["base_js_content"]).read_text(encoding="utf-8"))
    template_changes = []
    for old, new, expected, reason, reference in TEMPLATE_ADAPTATIONS.get(name, []):
        found = content.count(old)
        if found != expected:
            raise SystemExit(f"template adaptation expected {expected} match(es) of {old!r}, found {found}")
        content = content.replace(old, new)
        template_changes.append({"old": old, "new": new, "matches": found, "reason": reason, "reference": reference})
    record["template_adaptations" + ("" if wasm else "_ie")] = template_changes
    pieces = [("//module", module_js),
              ("//string_utf8", (COMMON_JS / "string_utf8.js").read_text(encoding="utf-8")),
              ("//desktop_fetch", (COMMON_JS / "desktop_fetch.js").read_text(encoding="utf-8"))]
    if not wasm:
        pieces.append(("//polyfill", (COMMON_JS / "polyfill.js").read_text(encoding="utf-8")))
    if name not in OFFICIAL_TEMPLATES:  # the official template already has these parts spliced in
        for key, path in (recipe.get("replaces") or {}).items():
            pieces.append(("//" + key, (work / path).read_text(encoding="utf-8")))
    # Like make.py, optional markers absent from a template are skipped (str.replace no-op); the
    # module itself must be spliced exactly once. Counts are recorded either way.
    counts = {}
    for marker, text in pieces:
        counts[marker] = content.count(marker)
        if marker == "//module" and counts[marker] != 1:
            raise SystemExit(f"template marker //module found {counts[marker]} times")
        content = content.replace(marker, text)
    deploy = out / "deploy"
    deploy.mkdir(parents=True, exist_ok=True)
    target = deploy / f"{name}{'' if wasm else '_ie'}.js"
    target.write_text(content, encoding="utf-8")
    binary = variant_dir / f"{name}{'.wasm' if wasm else '.js.mem'}"
    if binary.exists():
        (deploy / binary.name).write_bytes(binary.read_bytes())
    record["template_marker_counts" + ("" if wasm else "_ie")] = counts


def post_process_hash(deploy, record):
    """Historical DesktopEditor/xmlsec/src/wasm/hash/after.py with asserted counts."""
    base64_js = SDKJS_STRINGSERIALIZE.read_text(encoding="utf-8")
    window_count = base64_js.count("(window);")
    base64_js = base64_js.replace("(window);", "(self);")
    first = (deploy / "engine.js").read_text(encoding="utf-8")
    header = first[:first.find("*/") + 2]
    steps = []
    for filename in ("engine.js", "engine_ie.js"):
        path = deploy / filename
        text = path.read_text(encoding="utf-8")
        counts = {"license_header": text.count(header)}
        if counts["license_header"] != 1:
            raise SystemExit(f"{filename}: license header found {counts['license_header']} times")
        text = text.replace(header, "")
        counts["atpostrun"] = text.count("__ATPOSTRUN__=[];") + text.count("__ATPOSTRUN__ = [];")
        if counts["atpostrun"] != 1:
            raise SystemExit(f"{filename}: __ATPOSTRUN__ found {counts['atpostrun']} times")
        for form in ("__ATPOSTRUN__=[];", "__ATPOSTRUN__ = [];"):
            text = text.replace(form, "__ATPOSTRUN__=[function(){self.onEngineInit();}];")
        counts["getBinaryPromise"] = text.count("function getBinaryPromise(")
        if counts["getBinaryPromise"] != 1:
            raise SystemExit(f"{filename}: getBinaryPromise found {counts['getBinaryPromise']} times")
        text = text.replace("function getBinaryPromise(", "function getBinaryPromise2(")
        # Older-glue pattern; recorded even when absent from 3.1.48 output.
        counts["window_this"] = text.count("&&window===this?this")
        text = text.replace("&&window===this?this", "?window")
        counts["base64_marker"] = text.count("//base64")
        if counts["base64_marker"] != 1:
            raise SystemExit(f"{filename}: //base64 found {counts['base64_marker']} times")
        text = text.replace("//base64", base64_js)
        path.write_text(text, encoding="utf-8")
        steps.append({"file": filename, "counts": counts})
    record["post_process"] = {"stringserialize": str(SDKJS_STRINGSERIALIZE),
                              "stringserialize_sha256": sha256(SDKJS_STRINGSERIALIZE),
                              "window_to_self_replacements": window_count, "files": steps}


def official_template(name, wasm, record):
    """Official generated loader with its license header and Emscripten glue replaced by //module.

    Closure (WHITESPACE_ONLY, ES5 output) hoisted every function declaration of the loader's IIFE to its
    top while keeping their relative order, so the glue occupies two regions: its function declarations
    (from the glue's first function up to the first function of the template suffix) and its statements
    (from `var Module=` to the `run();` after the preInit block). Both are removed; everything else is the
    first-party template (including hoisted template prefix functions and polyfills).
    """
    import re
    config = OFFICIAL_TEMPLATES[name]
    path = config["wasm" if wasm else "asm"]
    text = path.read_text(encoding="utf-8")
    header = LICENSE_HEADER.read_text(encoding="utf-8")
    if not text.startswith(header):
        raise SystemExit(f"{path.name} does not start with Common/license/header.license")
    body = text[len(header):]

    def unique(pattern):
        found = [m for m in re.finditer(pattern, body)]
        if len(found) != 1:
            raise SystemExit(f"{path.name}: expected one match of {pattern!r}, found {len(found)}")
        return found[0]

    funcs_start = unique(r"function " + config["glue_first_function"] + r"\(").start()
    funcs_end = unique(r"function " + config["template_suffix_first_function"] + r"\(").start()
    module_decl = unique(r'var Module\s*=\s*typeof Module\s*!==?\s*["\']undefined["\']\s*\?\s*Module\s*:\s*\{\}\s*;').start()
    unique(r'if\(Module\["preInit"\]\)')
    stmt_end = re.compile(r'if\(Module\["preInit"\]\)\{.*?\}run\(\);', re.S).search(body, module_decl).end()
    if not funcs_start < funcs_end < module_decl < stmt_end:
        raise SystemExit(f"{path.name}: unexpected glue layout")
    glue_functions = re.findall(r"function ([A-Za-z_$][\w$]*)\(", body[funcs_start:funcs_end])
    leaked = sorted(set(glue_functions) & config["template_functions"])
    if leaked:
        raise SystemExit(f"{path.name}: template functions inside the glue function region: {leaked}")
    template = body[:funcs_start] + body[funcs_end:module_decl] + "//module" + body[stmt_end:]
    if "wasmExports" in template or template.count("//module") != 1:
        raise SystemExit(f"{path.name}: template still contains glue")
    record["official_template" + ("" if wasm else "_ie")] = {
        "source": str(path), "source_sha256": sha256(path),
        "glue_function_region": [funcs_start, funcs_end], "glue_functions_removed": len(glue_functions),
        "glue_statement_region": [module_decl, stmt_end], "template_chars": len(template)}
    return template


def post_process(name, deploy, record):
    if name == "engine":
        post_process_hash(deploy, record)
        return
    applied = []
    for suffix, alternatives, replacement, expected in AFTER.get(name, []):
        path = deploy / f"{name}{suffix}.js"
        text = path.read_text(encoding="utf-8")
        counts = {alt: text.count(alt) for alt in alternatives}
        if expected is not None and sum(counts.values()) != expected:
            raise SystemExit(f"{path.name}: expected {expected} match(es) for {alternatives}, got {counts}")
        for alt in alternatives:
            text = text.replace(alt, replacement)
        path.write_text(text, encoding="utf-8")
        applied.append({"file": path.name, "counts": counts, "replacement": replacement})
    record["post_process"] = applied


def minify(name, deploy, env, record):
    level = MINIFY.get(name)
    if not level:
        return
    header = LICENSE_HEADER.read_text(encoding="utf-8")
    results = []
    for suffix in ("", "_ie"):
        path = deploy / f"{name}{suffix}.js"
        unminified = path.with_suffix(".unminified.js")
        path.replace(unminified)
        minified = path.with_suffix(".closure.js")
        result = run([str(wasm_toolchain.CLOSURE_NATIVE), "--compilation_level", level,
                      "--language_out", "ECMASCRIPT5_STRICT", "--js_output_file", str(minified),
                      "--js", str(unminified)], deploy, env)
        results.append({k: result[k] for k in ("cmd", "exit", "stderr")})
        if result["exit"] != 0:
            raise SystemExit(f"closure failed for {path.name}: {result['stderr'][-2000:]}")
        path.write_text(header + minified.read_text(encoding="utf-8"), encoding="utf-8")
    record["minify"] = {"level": level, "language_out": "ECMASCRIPT5_STRICT", "runs": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--recipe", required=True, help="recipe path relative to build/wasm-source/core")
    parser.add_argument("--label", required=True)
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 4))
    parser.add_argument("--extra-flag", action="append", default=[],
                        help="diagnostic only: extra compiler/linker flag (recorded; not for deliverable builds)")
    args = parser.parse_args()

    recipe_path = CORE / args.recipe
    recipe = json.loads(recipe_path.read_text(encoding="utf-8"))
    work = recipe_path.parent
    adaptations = apply_adaptations(recipe, work)
    if args.extra_flag:
        recipe["compiler_flags"] = list(recipe["compiler_flags"]) + args.extra_flag
    name = recipe["name"]
    out = ROOT / "build" / "wasm-source" / "out" / name / args.label
    evidence = ROOT / "evidence" / "wasm-source" / f"{name}-build-{args.label}.json"
    if out.exists() or evidence.exists():
        sys.exit(f"label {args.label} already used for {name}")
    out.mkdir(parents=True)
    env = wasm_toolchain.toolchain_env()

    record = {"module": name, "label": args.label, "recipe": args.recipe, "recipe_sha256": sha256(recipe_path),
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "emscripten": "3.1.48", "em_config": str(wasm_toolchain.CONFIG), "jobs": args.jobs,
              "output_dir": str(out), "recipe_adaptations": adaptations,
              "diagnostic_extra_flags": args.extra_flag,
              "post_process_adaptations": POST_PROCESS_ADAPTATIONS.get(name, [])}
    status = "built"
    for wasm in (True, False):
        if not recipe.get("wasm" if wasm else "asm"):
            continue
        log = []
        summary = build_variant(recipe, work, out, wasm, args.jobs, env, log)
        variant = "wasm" if wasm else "asm"
        log_path = out / f"{variant}-commands.json"
        log_path.write_text(json.dumps(log, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        summary["command_log"] = str(log_path)
        record[variant] = summary
        if summary.get("compile_failures") or summary.get("partial_link_failure") or summary.get("link_exit"):
            status = f"{variant}_failed"
            break
        try:
            compose(recipe, work, out, wasm, record)
        except SystemExit as error:
            status, record["error"] = f"{variant}_compose_failed", str(error)
            break

    if status == "built":
        deploy = out / "deploy"
        try:
            post_process(name, deploy, record)
            minify(name, deploy, env, record)
        except SystemExit as error:
            status, record["error"] = "post_process_failed", str(error)
        record["deploy"] = {p.name: {"bytes": p.stat().st_size, "sha256": sha256(p)}
                            for p in sorted(deploy.iterdir()) if p.is_file()}
    record["status"] = status
    record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    evidence.parent.mkdir(parents=True, exist_ok=True)
    with open(evidence, "x", encoding="utf-8") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    brief = {k: record[k] for k in ("status", "error") if k in record}
    for variant in ("wasm", "asm"):
        if variant in record:
            brief[variant] = {k: v for k, v in record[variant].items() if k != "command_log"}
    print(json.dumps(brief, ensure_ascii=False, indent=1)[:6000])
    sys.exit(0 if status == "built" else 1)


if __name__ == "__main__":
    main()
