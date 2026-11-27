#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Writes the spreadsheet samples for the assistant's sheet commands (standard library only).

    python -I scripts/make_sheet_samples.py

fixtures/generated/sales-regions.xlsx has two worksheets:
  - 销售明细: 区域, 月份, 销售额, 成本 over 8 rows (to sort, filter and chart);
  - 名单: 姓名, 部门, 工号 with two repeated rows (to remove duplicates).
fixtures/generated/class-scores.xlsx (for the held-out scenarios, written after the rules):
  - 成绩: 姓名, 班级, 语文, 数学 over 7 rows;
  - 教师: 姓名, 科目.
An existing file is left alone unless --force is given.
"""

import argparse
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "fixtures" / "generated"

SALES = [
    ("销售明细", [
        ["区域", "月份", "销售额", "成本"],
        ["华东", "1月", 1200, 800],
        ["华北", "1月", 950, 600],
        ["华南", "1月", 1430, 900],
        ["华东", "2月", 1380, 850],
        ["华北", "2月", 870, 640],
        ["华南", "2月", 1610, 1000],
        ["华东", "3月", 1520, 920],
        ["华北", "3月", 1010, 700],
    ]),
    ("名单", [
        ["姓名", "部门", "工号"],
        ["张三", "销售部", 1001],
        ["李四", "财务部", 1002],
        ["张三", "销售部", 1001],
        ["王五", "销售部", 1003],
        ["李四", "财务部", 1002],
    ]),
]

SCORES = [
    ("成绩", [
        ["姓名", "班级", "语文", "数学"],
        ["张伟", "一班", 88, 92],
        ["王芳", "二班", 95, 78],
        ["李娜", "一班", 72, 85],
        ["刘洋", "三班", 81, 96],
        ["陈静", "二班", 90, 67],
        ["杨磊", "一班", 66, 99],
        ["赵敏", "三班", 84, 73],
    ]),
    ("教师", [
        ["姓名", "科目"],
        ["周老师", "语文"],
        ["吴老师", "数学"],
    ]),
]

FILES = {"sales-regions.xlsx": SALES, "class-scores.xlsx": SCORES}

HEAD = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
MAIN = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG = "http://schemas.openxmlformats.org/package/2006/relationships"


def column(index):
    name = ""
    index += 1
    while index:
        index, rest = divmod(index - 1, 26)
        name = chr(65 + rest) + name
    return name


def sheet_xml(rows):
    lines = []
    for r, row in enumerate(rows, start=1):
        cells = []
        for c, value in enumerate(row):
            ref = f"{column(c)}{r}"
            if isinstance(value, (int, float)):
                cells.append(f'<c r="{ref}"><v>{value}</v></c>')
            else:
                cells.append(f'<c r="{ref}" t="inlineStr"><is><t>{escape(value)}</t></is></c>')
        lines.append(f'<row r="{r}">{"".join(cells)}</row>')
    last = f"{column(max(len(row) for row in rows) - 1)}{len(rows)}"
    return (HEAD + f'<worksheet xmlns="{MAIN}" xmlns:r="{REL}"><dimension ref="A1:{last}"/>'
            f'<sheetData>{"".join(lines)}</sheetData></worksheet>')


def parts(SHEETS):
    sheet_type = "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"
    overrides = "".join(f'<Override PartName="/xl/worksheets/sheet{i}.xml" ContentType="{sheet_type}"/>'
                        for i in range(1, len(SHEETS) + 1))
    files = {
        "[Content_Types].xml": HEAD + '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="xml" ContentType="application/xml"/>'
        '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
        + overrides + "</Types>",
        "_rels/.rels": HEAD + f'<Relationships xmlns="{PKG}"><Relationship Id="rId1" '
        f'Type="{REL}/officeDocument" Target="xl/workbook.xml"/></Relationships>',
        "xl/workbook.xml": HEAD + f'<workbook xmlns="{MAIN}" xmlns:r="{REL}"><sheets>'
        + "".join(f'<sheet name="{escape(name)}" sheetId="{i}" r:id="rId{i}"/>' for i, (name, _) in enumerate(SHEETS, start=1))
        + "</sheets></workbook>",
        "xl/_rels/workbook.xml.rels": HEAD + f'<Relationships xmlns="{PKG}">'
        + "".join(f'<Relationship Id="rId{i}" Type="{REL}/worksheet" Target="worksheets/sheet{i}.xml"/>'
                  for i in range(1, len(SHEETS) + 1))
        + "</Relationships>",
    }
    for i, (_, rows) in enumerate(SHEETS, start=1):
        files[f"xl/worksheets/sheet{i}.xml"] = sheet_xml(rows)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--force", action="store_true", help="replace an existing file")
    args = parser.parse_args()
    GENERATED.mkdir(parents=True, exist_ok=True)
    for file, sheets in FILES.items():
        target = GENERATED / file
        if target.exists() and not args.force:
            print(f"{target} exists; left alone")
            continue
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, text in parts(sheets).items():
                archive.writestr(name, text.encode("utf-8"))
        print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
