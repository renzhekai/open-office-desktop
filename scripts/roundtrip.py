"""Generate small OOXML fixtures and check semantic round trips (stdlib only).

This does not launch ONLYOFFICE or establish visual fidelity. An engine save is a
separate operation. A successful comparison is never presented as a source build,
runtime, rendering, or complete compatibility pass.
"""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation
import json
from pathlib import Path, PurePosixPath
import posixpath
import sys
from typing import Any
from xml.etree import ElementTree as ET
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile


W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
S = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
P = "http://schemas.openxmlformats.org/presentationml/2006/main"
A = "http://schemas.openxmlformats.org/drawingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
REL = "http://schemas.openxmlformats.org/package/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
NS = {"w": W, "s": S, "p": P, "a": A, "r": R}
FIXTURES = ("text-table.docx", "values-formula.xlsx", "text-shape.pptx")
MAX_UNCOMPRESSED_BYTES = 32 * 1024 * 1024


class PackageError(ValueError):
    """The file cannot safely be inspected as a supported OOXML package."""


def xml(text: str) -> bytes:
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n' + text).encode("utf-8")


def relationships(entries: list[tuple[str, str, str]]) -> bytes:
    root = ET.Element(f"{{{REL}}}Relationships")
    for rid, kind, target in entries:
        ET.SubElement(root, f"{{{REL}}}Relationship", Id=rid, Type=R + "/" + kind, Target=target)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def content_types(overrides: dict[str, str]) -> bytes:
    root = ET.Element(f"{{{CT}}}Types")
    ET.SubElement(root, f"{{{CT}}}Default", Extension="rels", ContentType="application/vnd.openxmlformats-package.relationships+xml")
    ET.SubElement(root, f"{{{CT}}}Default", Extension="xml", ContentType="application/xml")
    for name, kind in overrides.items():
        ET.SubElement(root, f"{{{CT}}}Override", PartName="/" + name, ContentType=kind)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def document_parts() -> dict[str, bytes]:
    return {
        "[Content_Types].xml": content_types({
            "word/document.xml": "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
            "word/styles.xml": "application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml",
        }),
        "_rels/.rels": relationships([("rId1", "officeDocument", "word/document.xml")]),
        "word/_rels/document.xml.rels": relationships([("rId1", "styles", "styles.xml")]),
        "word/styles.xml": xml(f'''<w:styles xmlns:w="{W}">
          <w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Arial" w:hAnsi="Arial" w:eastAsia="Microsoft YaHei"/><w:sz w:val="24"/></w:rPr></w:rPrDefault></w:docDefaults>
          <w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
        </w:styles>'''),
        "word/document.xml": xml(f'''<w:document xmlns:w="{W}"><w:body>
          <w:p><w:r><w:rPr><w:b/><w:color w:val="204060"/><w:sz w:val="32"/></w:rPr><w:t>Open Office 兼容性样本</w:t></w:r></w:p>
          <w:p><w:r><w:t>中文与 English 123；标点：，。！？</w:t></w:r></w:p>
          <w:p><w:r><w:t xml:space="preserve">普通文字 + </w:t></w:r><w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>粗斜体</w:t></w:r></w:p>
          <w:tbl><w:tblPr><w:tblW w:w="8000" w:type="dxa"/><w:tblBorders><w:top w:val="single" w:sz="4"/><w:left w:val="single" w:sz="4"/><w:bottom w:val="single" w:sz="4"/><w:right w:val="single" w:sz="4"/><w:insideH w:val="single" w:sz="4"/><w:insideV w:val="single" w:sz="4"/></w:tblBorders></w:tblPr>
            <w:tblGrid><w:gridCol w:w="4000"/><w:gridCol w:w="4000"/></w:tblGrid>
            <w:tr><w:tc><w:tcPr><w:tcW w:w="4000" w:type="dxa"/></w:tcPr><w:p><w:r><w:t>项目</w:t></w:r></w:p></w:tc><w:tc><w:tcPr><w:tcW w:w="4000" w:type="dxa"/></w:tcPr><w:p><w:r><w:t>数量</w:t></w:r></w:p></w:tc></w:tr>
            <w:tr><w:tc><w:tcPr><w:tcW w:w="4000" w:type="dxa"/></w:tcPr><w:p><w:r><w:t>办公套件</w:t></w:r></w:p></w:tc><w:tc><w:tcPr><w:tcW w:w="4000" w:type="dxa"/></w:tcPr><w:p><w:r><w:t>3</w:t></w:r></w:p></w:tc></w:tr>
          </w:tbl>
          <w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/></w:sectPr>
        </w:body></w:document>'''),
    }


def spreadsheet_parts() -> dict[str, bytes]:
    return {
        "[Content_Types].xml": content_types({
            "xl/workbook.xml": "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
            "xl/worksheets/sheet1.xml": "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
            "xl/styles.xml": "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml",
        }),
        "_rels/.rels": relationships([("rId1", "officeDocument", "xl/workbook.xml")]),
        "xl/_rels/workbook.xml.rels": relationships([
            ("rId1", "worksheet", "worksheets/sheet1.xml"), ("rId2", "styles", "styles.xml")]),
        "xl/workbook.xml": xml(f'''<workbook xmlns="{S}" xmlns:r="{R}"><sheets><sheet name="兼容性" sheetId="1" r:id="rId1"/></sheets><calcPr calcId="191029" fullCalcOnLoad="1"/></workbook>'''),
        "xl/styles.xml": xml(f'''<styleSheet xmlns="{S}">
          <fonts count="2"><font><sz val="11"/><name val="Arial"/></font><font><b/><sz val="11"/><color rgb="FF204060"/><name val="Arial"/></font></fonts>
          <fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>
          <borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
          <cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
          <cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>
          <cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
        </styleSheet>'''),
        "xl/worksheets/sheet1.xml": xml(f'''<worksheet xmlns="{S}"><dimension ref="A1:C3"/><sheetViews><sheetView workbookViewId="0"/></sheetViews><sheetFormatPr defaultRowHeight="15"/>
          <cols><col min="1" max="3" width="18" customWidth="1"/></cols><sheetData>
          <row r="1"><c r="A1" s="1" t="inlineStr"><is><t>数量 A</t></is></c><c r="B1" s="1" t="inlineStr"><is><t>数量 B</t></is></c><c r="C1" s="1" t="inlineStr"><is><t>总计</t></is></c></row>
          <row r="2"><c r="A2"><v>2</v></c><c r="B2"><v>3</v></c><c r="C2"><f>SUM(A2:B2)</f><v>5</v></c></row>
          <row r="3"><c r="A3" t="inlineStr"><is><t>中文 English 123</t></is></c><c r="B3"><v>0.125</v></c><c r="C3" t="b"><v>1</v></c></row>
          </sheetData></worksheet>'''),
    }


def theme() -> bytes:
    colors = [('dk1', '000000'), ('lt1', 'FFFFFF'), ('dk2', '204060'), ('lt2', 'EEEEEE'),
              ('accent1', '4472C4'), ('accent2', 'ED7D31'), ('accent3', 'A5A5A5'),
              ('accent4', 'FFC000'), ('accent5', '5B9BD5'), ('accent6', '70AD47'),
              ('hlink', '0563C1'), ('folHlink', '954F72')]
    color_xml = ''.join(f'<a:{name}><a:srgbClr val="{value}"/></a:{name}>' for name, value in colors)
    fill = '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>'
    line = '<a:ln w="12700">' + fill + '<a:prstDash val="solid"/><a:miter lim="800000"/></a:ln>'
    return xml(f'''<a:theme xmlns:a="{A}" name="Compatibility"><a:themeElements>
      <a:clrScheme name="Compatibility">{color_xml}</a:clrScheme>
      <a:fontScheme name="Compatibility"><a:majorFont><a:latin typeface="Arial"/><a:ea typeface="Microsoft YaHei"/><a:cs typeface="Arial"/></a:majorFont><a:minorFont><a:latin typeface="Arial"/><a:ea typeface="Microsoft YaHei"/><a:cs typeface="Arial"/></a:minorFont></a:fontScheme>
      <a:fmtScheme name="Compatibility"><a:fillStyleLst>{fill * 3}</a:fillStyleLst><a:lnStyleLst>{line * 3}</a:lnStyleLst><a:effectStyleLst>{'<a:effectStyle><a:effectLst/></a:effectStyle>' * 3}</a:effectStyleLst><a:bgFillStyleLst>{fill * 3}</a:bgFillStyleLst></a:fmtScheme>
      </a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>''')


def presentation_parts() -> dict[str, bytes]:
    group = '''<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>'''
    color_map = '<p:clrMap accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" bg1="lt1" bg2="lt2" folHlink="folHlink" hlink="hlink" tx1="dk1" tx2="dk2"/>'
    return {
        "[Content_Types].xml": content_types({
            "ppt/presentation.xml": "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml",
            "ppt/slides/slide1.xml": "application/vnd.openxmlformats-officedocument.presentationml.slide+xml",
            "ppt/slideMasters/slideMaster1.xml": "application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml",
            "ppt/slideLayouts/slideLayout1.xml": "application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml",
            "ppt/theme/theme1.xml": "application/vnd.openxmlformats-officedocument.theme+xml",
        }),
        "_rels/.rels": relationships([("rId1", "officeDocument", "ppt/presentation.xml")]),
        "ppt/presentation.xml": xml(f'''<p:presentation xmlns:p="{P}" xmlns:a="{A}" xmlns:r="{R}"><p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst><p:sldIdLst><p:sldId id="256" r:id="rId2"/></p:sldIdLst><p:sldSz cx="12192000" cy="6858000" type="screen16x9"/><p:notesSz cx="6858000" cy="9144000"/></p:presentation>'''),
        "ppt/_rels/presentation.xml.rels": relationships([("rId1", "slideMaster", "slideMasters/slideMaster1.xml"), ("rId2", "slide", "slides/slide1.xml")]),
        "ppt/slides/slide1.xml": xml(f'''<p:sld xmlns:p="{P}" xmlns:a="{A}" xmlns:r="{R}"><p:cSld><p:spTree>{group}
          <p:sp><p:nvSpPr><p:cNvPr id="2" name="Title"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="914400" y="685800"/><a:ext cx="10363200" cy="1143000"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="zh-CN" sz="3200" b="1"/><a:t>Open Office 演示样本</a:t></a:r><a:endParaRPr lang="zh-CN"/></a:p></p:txBody></p:sp>
          <p:sp><p:nvSpPr><p:cNvPr id="3" name="Rectangle"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="1828800" y="2743200"/><a:ext cx="8229600" cy="1828800"/></a:xfrm><a:prstGeom prst="roundRect"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val="DCE6F1"/></a:solidFill><a:ln w="12700"><a:solidFill><a:srgbClr val="204060"/></a:solidFill></a:ln></p:spPr><p:txBody><a:bodyPr anchor="ctr"/><a:lstStyle/><a:p><a:pPr algn="ctr"/><a:r><a:rPr lang="zh-CN" sz="2400"/><a:t>中文 / English / 123</a:t></a:r></a:p></p:txBody></p:sp>
          </p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>'''),
        "ppt/slides/_rels/slide1.xml.rels": relationships([("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml")]),
        "ppt/slideMasters/slideMaster1.xml": xml(f'''<p:sldMaster xmlns:p="{P}" xmlns:a="{A}" xmlns:r="{R}"><p:cSld><p:spTree>{group}</p:spTree></p:cSld>{color_map}<p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst><p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></p:sldMaster>'''),
        "ppt/slideMasters/_rels/slideMaster1.xml.rels": relationships([("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"), ("rId2", "theme", "../theme/theme1.xml")]),
        "ppt/slideLayouts/slideLayout1.xml": xml(f'''<p:sldLayout xmlns:p="{P}" xmlns:a="{A}" xmlns:r="{R}" type="blank" preserve="1"><p:cSld name="Blank"><p:spTree>{group}</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>'''),
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels": relationships([("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")]),
        "ppt/theme/theme1.xml": theme(),
    }


def generate(directory: Path) -> dict[str, Any]:
    directory.mkdir(parents=True, exist_ok=True)
    targets = [directory / name for name in FIXTURES]
    conflicts = [str(path) for path in targets if path.exists()]
    if conflicts:
        raise FileExistsError("Refusing to overwrite existing fixtures: " + ", ".join(conflicts))
    for target, parts in zip(targets, [document_parts(), spreadsheet_parts(), presentation_parts()]):
        with target.open("xb") as stream, ZipFile(stream, "w", ZIP_DEFLATED) as archive:
            for name, content in parts.items():
                archive.writestr(name, content)
    return {"status": "fixtures_generated", "engine_executed": False,
            "files": [{"path": str(path), "snapshot": snapshot(path)} for path in targets],
            "limitation": "Package/XML smoke checks only; not XSD validation or an editor open/save test."}


class Package:
    def __init__(self, path: Path):
        try:
            with ZipFile(path) as archive:
                entries = archive.infolist()
                names = [item.filename for item in entries]
                if len(names) != len(set(names)):
                    raise PackageError("Duplicate ZIP entries")
                if sum(item.file_size for item in entries) > MAX_UNCOMPRESSED_BYTES:
                    raise PackageError("Package exceeds the 32 MiB smoke-test inspection limit")
                if archive.testzip() is not None:
                    raise PackageError("ZIP CRC check failed")
                self.parts = {name: archive.read(name) for name in names if not name.endswith("/")}
        except (BadZipFile, RuntimeError, OSError) as exc:
            raise PackageError(str(exc)) from exc
        for required in ("[Content_Types].xml", "_rels/.rels"):
            if required not in self.parts:
                raise PackageError(f"Missing package part: {required}")
        for name in self.parts:
            if name.endswith((".xml", ".rels")):
                self.tree(name)
            if name.endswith(".rels"):
                self.targets(name)
        root_targets = self.targets("_rels/.rels")
        main = [entry[0] for entry in root_targets.values() if entry[1].endswith("/officeDocument")]
        if len(main) != 1:
            raise PackageError("Expected exactly one officeDocument relationship")
        self.main = main[0]

    def tree(self, name: str) -> ET.Element:
        try:
            return ET.fromstring(self.parts[name])
        except (KeyError, ET.ParseError) as exc:
            raise PackageError(f"Invalid or missing XML part {name}: {exc}") from exc

    def targets(self, rel_path: str) -> dict[str, tuple[str, str]]:
        result: dict[str, tuple[str, str]] = {}
        root = self.tree(rel_path)
        if root.tag != f"{{{REL}}}Relationships":
            raise PackageError(f"Wrong relationships root in {rel_path}")
        base = "" if rel_path == "_rels/.rels" else str(PurePosixPath(rel_path).parent.parent)
        for item in root:
            rid = item.get("Id", "")
            if not rid or rid in result:
                raise PackageError(f"Missing or duplicate relationship ID in {rel_path}")
            if item.get("TargetMode") == "External":
                result[rid] = (item.get("Target", ""), "external")
                continue
            target = item.get("Target", "")
            target = posixpath.normpath(target.lstrip("/") if target.startswith("/") else posixpath.join(base, target))
            if target.startswith("../") or target not in self.parts:
                raise PackageError(f"Missing internal relationship target {target} in {rel_path}")
            result[rid] = (target, item.get("Type", ""))
        return result

    def part_targets(self, part: str) -> dict[str, tuple[str, str]]:
        source = PurePosixPath(part)
        rel_path = str(source.parent / "_rels" / (source.name + ".rels"))
        return self.targets(rel_path) if rel_path in self.parts else {}


def paragraph_text(paragraph: ET.Element, namespace: str) -> str:
    chunks: list[str] = []
    for element in paragraph.iter():
        if element.tag == f"{{{namespace}}}t":
            chunks.append(element.text or "")
        elif element.tag == f"{{{namespace}}}tab":
            chunks.append("\t")
        elif element.tag in (f"{{{namespace}}}br", f"{{{namespace}}}cr"):
            chunks.append("\n")
    return "".join(chunks)


def number(value: str) -> str:
    try:
        parsed = Decimal(value)
        if not parsed.is_finite():
            raise PackageError("Non-finite numeric cell")
        return str(parsed.normalize()) if parsed else "0"
    except InvalidOperation as exc:
        raise PackageError(f"Invalid numeric cell: {value}") from exc


def snapshot(path: Path) -> dict[str, Any]:
    package = Package(path)
    root = package.tree(package.main)
    if root.tag == f"{{{W}}}document":
        paragraphs = [paragraph_text(p, W) for p in root.findall(".//w:p", NS)]
        tables = [[["\n".join(paragraph_text(p, W) for p in cell.findall(".//w:p", NS))
                    for cell in row.findall("w:tc", NS)] for row in table.findall("w:tr", NS)]
                  for table in root.findall(".//w:tbl", NS)]
        return {"kind": "docx", "paragraphs": [p for p in paragraphs if p], "tables": tables}
    if root.tag == f"{{{S}}}workbook":
        rels = package.part_targets(package.main)
        shared = [value[0] for value in rels.values() if value[1].endswith("/sharedStrings")]
        strings = ["".join(t.text or "" for t in item.iter(f"{{{S}}}t"))
                   for item in package.tree(shared[0])] if shared else []
        sheets: list[dict[str, Any]] = []
        for sheet in root.findall("s:sheets/s:sheet", NS):
            rid = sheet.get(f"{{{R}}}id", "")
            if rid not in rels or not rels[rid][1].endswith("/worksheet"):
                raise PackageError(f"Worksheet relationship missing: {rid}")
            cells: dict[str, Any] = {}
            for cell in package.tree(rels[rid][0]).findall("s:sheetData/s:row/s:c", NS):
                address = cell.get("r", "")
                if not address or address in cells:
                    raise PackageError("Missing or duplicate cell address")
                formula = cell.find("s:f", NS)
                value = cell.findtext("s:v", default="", namespaces=NS)
                cell_type = cell.get("t", "n")
                if formula is not None:
                    if formula.get("t") == "shared" or not formula.text:
                        raise PackageError("Shared/empty formulas need a larger regression inspector")
                    cells[address] = {"formula": formula.text.lstrip("=")}
                elif cell_type == "inlineStr":
                    cells[address] = {"text": "".join(t.text or "" for t in cell.findall("s:is//s:t", NS))}
                elif cell_type == "s":
                    try:
                        index = int(value)
                        if index < 0:
                            raise IndexError(index)
                        cells[address] = {"text": strings[index]}
                    except (ValueError, IndexError) as exc:
                        raise PackageError(f"Invalid shared string index: {value}") from exc
                elif cell_type == "str":
                    cells[address] = {"text": value}
                elif value:
                    cells[address] = {"number": number(value)} if cell_type == "n" else {cell_type: value}
            sheets.append({"name": sheet.get("name"), "cells": cells})
        return {"kind": "xlsx", "sheets": sheets}
    if root.tag == f"{{{P}}}presentation":
        rels = package.part_targets(package.main)
        slides: list[dict[str, Any]] = []
        for slide_id in root.findall("p:sldIdLst/p:sldId", NS):
            rid = slide_id.get(f"{{{R}}}id", "")
            if rid not in rels or not rels[rid][1].endswith("/slide"):
                raise PackageError(f"Slide relationship missing: {rid}")
            slide = package.tree(rels[rid][0])
            text = [paragraph_text(p, A) for p in slide.findall(".//a:p", NS)]
            shapes = Counter(geometry.get("prst", "") for geometry in slide.findall(".//p:sp/p:spPr/a:prstGeom", NS))
            slides.append({"paragraphs": [p for p in text if p], "preset_shapes": dict(sorted(shapes.items()))})
        return {"kind": "pptx", "slides": slides}
    raise PackageError(f"Unsupported main document root: {root.tag}")


def differences(expected: Any, actual: Any, pointer: str = "$") -> list[dict[str, Any]]:
    if type(expected) is not type(actual):
        return [{"path": pointer, "expected": expected, "actual": actual}]
    if isinstance(expected, dict):
        result = []
        for key in sorted(set(expected) | set(actual)):
            if key not in expected or key not in actual:
                result.append({"path": f"{pointer}.{key}", "expected": expected.get(key), "actual": actual.get(key)})
            else:
                result.extend(differences(expected[key], actual[key], f"{pointer}.{key}"))
        return result
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return [{"path": pointer, "expected": expected, "actual": actual}]
        return [difference for index, (a, b) in enumerate(zip(expected, actual))
                for difference in differences(a, b, f"{pointer}[{index}]")]
    return [] if expected == actual else [{"path": pointer, "expected": expected, "actual": actual}]


def compare(inputs: Path, outputs: Path, evidence: Path | None = None) -> tuple[dict[str, Any], int]:
    results = []
    for name in FIXTURES:
        source, target = inputs / name, outputs / name
        item: dict[str, Any] = {"file": name, "input": str(source), "output": str(target)}
        if not source.is_file():
            item.update(status="invalid_input", reason="Input fixture missing")
        elif not target.is_file():
            item.update(status="not_executed", reason="No editor-saved output exists")
        elif source.resolve() == target.resolve():
            item.update(status="invalid_output", reason="Input and output must be different files")
        else:
            try:
                delta = differences(snapshot(source), snapshot(target))
                item.update(status="semantic_mismatch" if delta else "semantic_match", differences=delta)
            except PackageError as exc:
                item.update(status="invalid_package", reason=str(exc))
        results.append(item)
    run_evidence: dict[str, Any] = {"status": "not_provided", "engine_execution_verified": False}
    if evidence is not None:
        try:
            declared = json.loads(evidence.read_text(encoding="utf-8"))
            required = {"engine", "version", "operation", "saved_files", "run_note"}
            if not isinstance(declared, dict) or not required <= declared.keys():
                raise ValueError("Evidence needs engine, version, operation, saved_files, run_note")
            if declared["engine"] != "ONLYOFFICE Desktop Editors" or declared["operation"] != "open-save-reopen":
                raise ValueError("Evidence must identify ONLYOFFICE Desktop Editors open-save-reopen")
            if not isinstance(declared["saved_files"], list) or not set(FIXTURES) <= set(declared["saved_files"]):
                raise ValueError("Evidence must name all three saved fixture files")
            if not all(isinstance(declared[key], str) and declared[key].strip() for key in ("version", "run_note")):
                raise ValueError("Version and run note must be non-empty strings")
            run_evidence.update(status="operator_declared", declaration=declared,
                                limitation="Operator declaration; this script cannot authenticate application provenance")
        except (OSError, ValueError, TypeError) as exc:
            run_evidence.update(status="invalid", reason=str(exc))
    states = {item["status"] for item in results}
    if states - {"not_executed", "semantic_match"}:
        status, exit_code = "failed", 1
    elif "not_executed" in states:
        status, exit_code = "not_executed", 2
    elif run_evidence["status"] != "operator_declared":
        status, exit_code = "semantic_match_unverified_provenance", 2
    else:
        status, exit_code = "semantic_checks_passed_with_operator_evidence", 0
    return {"status": status, "checked_at_utc": datetime.now(timezone.utc).isoformat(),
            "results": results, "runtime_evidence": run_evidence,
            "visual_fidelity": "not_tested", "source_build": "not_tested",
            "scope": "Minimal text/table/cell/formula/slide/preset-shape checks only; no full OOXML conformance, style or layout guarantee."}, exit_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generator = commands.add_parser("generate", help="Create three new fixtures; never overwrite files")
    generator.add_argument("--directory", type=Path, default=Path("fixtures/generated"))
    inspector = commands.add_parser("inspect", help="Inspect OOXML structure and supported semantics")
    inspector.add_argument("file", type=Path)
    comparator = commands.add_parser("compare", help="Compare fixtures against separate editor-saved outputs")
    comparator.add_argument("--inputs", type=Path, default=Path("fixtures/generated"))
    comparator.add_argument("--outputs", type=Path, default=Path("fixtures/roundtrip"))
    comparator.add_argument("--evidence", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "generate":
            result, code = generate(args.directory), 0
        elif args.command == "inspect":
            result, code = {"status": "package_inspected", "snapshot": snapshot(args.file), "engine_executed": False}, 0
        else:
            result, code = compare(args.inputs, args.outputs, args.evidence)
    except (OSError, PackageError) as exc:
        result, code = {"status": "error", "error": str(exc), "engine_executed": False}, 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return code


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
