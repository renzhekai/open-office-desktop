#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Spreadsheets and presentations in the real editor: open, change, save; new, change, save as; reopen.

Starts OpenOffice.exe from a runtime with the MCP test server and the engine's debug port
(a fresh profile and settings of its own), then for XLSX and PPTX:
  - opens a copy of a fixture, checks the editor is editable (not read-only), changes it the way
    the editor does (a cell's text; a new slide with a text box), saves (document.save), and finds
    the text in the saved file;
  - creates a new document (document.create), changes it, saves it to a path (test.saveAs) and
    finds the text in that file;
  - opens that file again and reads the text back in the editor;
  - runs the assistant's own commands for the kind through MCP (sheet.read, cells.set with a
    formula, cells.format, edit.undo, save; slides.read, slide.add with and without layouts,
    slide.setText, edit.undo, save as) and checks what they did.
The changes go through the editor's own API in the editor frame (scripts/editor_eval.mjs);
the assistant's commands are for text documents only. A record goes to
evidence/office-kinds/<label>.json.

Usage:
    python -I scripts/verify_office_kinds.py kinds-r18 --runtime build/full-source-run-builtin-ai-r18
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "office-kinds"
EVIDENCE = ROOT / "evidence" / "office-kinds"
FIXTURES = ROOT / "fixtures" / "generated"
EVAL = ROOT / "scripts" / "editor_eval.mjs"


class Mcp:
    def __init__(self, endpoint):
        self.url = endpoint["url"]
        self.token = endpoint["token"]
        self.next_id = 0
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # loopback, never a proxy

    def call(self, tool, arguments=None, timeout=300):
        self.next_id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": "tools/call",
                           "params": {"name": tool, "arguments": arguments or {}}}).encode("utf-8")
        request = urllib.request.Request(self.url, data=body, method="POST", headers={
            "Content-Type": "application/json", "Authorization": "Bearer " + self.token})
        with self.opener.open(request, timeout=timeout) as response:
            answer = json.loads(response.read().decode("utf-8"))
        if "error" in answer:
            raise RuntimeError(f"{tool}: {answer['error']}")
        result = answer["result"]
        return result.get("structuredContent", {}), bool(result.get("isError"))


def editor(code):
    result = subprocess.run(["node", str(EVAL), "-e", code], capture_output=True, timeout=120)
    text = result.stdout.decode("utf-8", "replace").strip()
    try:
        return json.loads(text.splitlines()[-1]) if text else None
    except json.JSONDecodeError:
        return {"unreadable": text[:500], "stderr": result.stderr.decode("utf-8", "replace")[:500]}


WAIT = "await new Promise(function (r) { setTimeout(r, %d); });"
STATE = "var a = Asc.editor; return {type: a.constructor.name, viewMode: a.isViewMode, modified: a.isDocumentModified()};"
SHEET_WRITE = (WAIT % 1500 + "var a = Asc.editor; a.asc_findCell('%s');" + WAIT % 300
               + "a.pluginMethod_PasteText(%s);" + WAIT % 1200
               + "var ws = a.wbModel.getActiveWs(); return {value: ws.getCell3(%d, %d).getValue(), viewMode: a.isViewMode, modified: a.isDocumentModified()};")
SHEET_READ = (WAIT % 1500 + "var ws = Asc.editor.wbModel.getActiveWs(); var out = [];"
              "for (var r = 0; r < 6; r++) for (var c = 0; c < 6; c++) { var v = ws.getCell3(r, c).getValue(); if (v) out.push(v); }"
              "return {values: out, viewMode: Asc.editor.isViewMode};")
SLIDE_TEXTS = ("var p = Asc.editor.WordControl.m_oLogicDocument; var out = [];"
               "for (var i = 0; i < p.Slides.length; i++) { var tree = p.Slides[i].cSld.spTree;"
               " for (var j = 0; j < tree.length; j++) { var dc = tree[j].getDocContent && tree[j].getDocContent();"
               "  if (dc) { var t = ''; dc.Content.forEach(function (x) { t += x.GetText ? x.GetText() : ''; }); out.push(t.trim()); } } }")
SLIDE_WRITE = (WAIT % 1500 + "var a = Asc.editor; var p = a.WordControl.m_oLogicDocument; var before = p.Slides.length; %s"
               + WAIT % 800 + "a.pluginMethod_PasteText(%s);" + WAIT % 1500 + SLIDE_TEXTS
               + "return {slidesBefore: before, slides: p.Slides.length, texts: out, viewMode: a.isViewMode, modified: a.isDocumentModified()};")
SLIDE_READ = WAIT % 1500 + SLIDE_TEXTS + "return {texts: out, viewMode: Asc.editor.isViewMode};"


def file_has(path, text, prefix):
    with zipfile.ZipFile(path) as archive:
        return any(text in archive.read(name).decode("utf-8", "replace")
                   for name in archive.namelist() if name.startswith(prefix) and name.endswith(".xml"))


def commands(mcp, kind, step, work, prefix):
    """The assistant's own commands for this kind, called through MCP on the open document."""
    def value(tool, arguments=None):
        content, error = mcp.call(tool, arguments or {})
        return (None if error else content.get("value", {})), content

    if kind == "spreadsheet":
        read, content = value("sheet.read")
        step("sheet.read", read and read.get("rows", [[None]])[0][0] == "数量 A" and read.get("formulas", {}).get("C2") == "=SUM(A2:B2)",
             value=read, message=content.get("message"))
        done, content = value("cells.set", {"start": "E1", "rows": [["乘积"], ["=A2*B2"]]})
        read, _ = value("sheet.read", {"range": "E1:E2"})
        step("cells.set (a formula)", done is not None and read and read.get("rows") == [["乘积"], ["6"]]
             and read.get("formulas", {}).get("E2") == "=A2*B2", message=content.get("message"), read=read)
        done, content = value("cells.format", {"range": "A1:E1", "bold": True, "fill": "#D9E2F3"})
        read, _ = value("sheet.read", {"range": "A1:E1"})
        styles = (read or {}).get("styles", {})
        step("cells.format", done is not None and styles.get("A1", {}).get("bold") and styles.get("E1", {}).get("fill") == "#D9E2F3",
             message=content.get("message"), styles=styles)
        done, content = value("edit.undo")
        read, _ = value("sheet.read", {"range": "A1:E1"})
        step("edit.undo (the format)", done is not None and "fill" not in (read or {}).get("styles", {}).get("E1", {}),
             message=content.get("message"), styles=(read or {}).get("styles"))
        content, error = mcp.call("document.save")
        saved = work / "existing.xlsx"
        step("saved with the formula", not error and file_has(saved, "A2*B2", "xl/") and file_has(saved, "乘积", "xl/"))
        return
    # A presentation of plain text boxes: the first box is the title, and that is what changes.
    done, content = value("slide.setText", {"slide": 1, "part": "title", "text": "改过的第一页标题"})
    read, _ = value("slides.read")
    step("slide.setText on text boxes", done is not None and read["slides"][0].get("title") == "改过的第一页标题",
         message=content.get("message"), slide=read["slides"][0] if read else None)
    # Only a blank layout to use: the new slide is made of text boxes.
    done, content = value("slide.add", {"title": "命令加的页", "points": ["要点一", "要点二"], "position": "end"})
    read, _ = value("slides.read")
    last = (read or {}).get("slides", [{}])[-1]
    texts = [last.get("title", "")] + last.get("body", []) + last.get("other", [])
    step("slide.add (no layout to use)", done is not None and "命令加的页" in texts and "要点二" in texts,
         message=content.get("message"), slide=last)
    # A new presentation has the usual layouts: title and content placeholders.
    mcp.call("document.save")  # the current one first: no question about unsaved changes
    mcp.call("document.create", {"kind": "presentation"})
    editor(WAIT % 2500 + STATE)
    done, content = value("slide.add", {"title": "项目进度", "points": ["需求已完成", "开发进行中", "下周测试"]})
    read, _ = value("slides.read")
    second = (read or {}).get("slides", [{}, {}])[1] if read and len(read.get("slides", [])) > 1 else {}
    step("slide.add (title and content)", done is not None and second.get("title") == "项目进度"
         and second.get("body") == ["需求已完成", "开发进行中", "下周测试"], message=content.get("message"), slide=second)
    done, content = value("slide.setText", {"slide": 2, "part": "title", "text": "改过的标题"})
    read, _ = value("slides.read")
    step("slide.setText", done is not None and read["slides"][1].get("title") == "改过的标题", message=content.get("message"))
    done, content = value("edit.undo")
    read, _ = value("slides.read")
    step("edit.undo (the title)", done is not None and read["slides"][1].get("title") == "项目进度", message=content.get("message"))
    target = work / "commands.pptx"
    content, error = mcp.call("test.saveAs", {"path": str(target)})
    step("saved with the slide", not error and target.is_file() and file_has(target, "开发进行中", prefix))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("label")
    parser.add_argument("--runtime", type=Path, required=True)
    args = parser.parse_args()
    runtime = (args.runtime if args.runtime.is_absolute() else ROOT / args.runtime).resolve()
    work = WORK / args.label
    record_path = EVIDENCE / f"{args.label}.json"
    if work.exists() or record_path.exists():
        sys.exit(f"{work} or {record_path} exists; pick a new label")
    profile = work / "profile"
    (profile / "data").mkdir(parents=True)
    (profile / "data" / "settings.xml").write_text(
        "<Settings><ascdesktop-support-debug-info-keep>1</ascdesktop-support-debug-info-keep></Settings>", encoding="utf-8")
    environment = dict(os.environ)
    for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "OPEN_OFFICE_AI_ACTIVE", "OPEN_OFFICE_AI_ENGINE_URL"):
        environment.pop(name, None)
    environment["PATH"] = os.pathsep.join([str(runtime), str(Path(os.environ["SystemRoot"]) / "System32")])
    environment["OPEN_OFFICE_SETTINGS_FILE"] = str(work / "settings.ini")  # never the person's settings
    environment["OPEN_OFFICE_AI_BACKEND"] = "builtin"
    command = [str(runtime / "OpenOffice.exe"), "--engine-profile", str(profile), "--mcp-port", "0", "--mcp-test"]
    record = {"label": args.label, "runtime": str(runtime), "command": command,
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "steps": []}
    with open(work / "stdout.log", "wb") as out, open(work / "stderr.log", "wb") as err:
        process = subprocess.Popen(command, cwd=runtime, env=environment, stdout=out, stderr=err)

    def step(name, ok, **details):
        record["steps"].append({"step": name, "ok": bool(ok), **details})
        print(json.dumps({"step": name, "ok": bool(ok)}, ensure_ascii=False))

    try:
        endpoint = profile / "mcp-endpoint.json"
        deadline = time.time() + 90
        while not endpoint.is_file() and time.time() < deadline and process.poll() is None:
            time.sleep(0.5)
        if not endpoint.is_file():
            raise RuntimeError(f"no MCP endpoint (exit code {process.poll()})")
        mcp = Mcp(json.loads(endpoint.read_text(encoding="utf-8")))
        for kind, fixture, prefix in (("spreadsheet", "values-formula.xlsx", "xl/"), ("presentation", "text-shape.pptx", "ppt/slides/")):
            extension = fixture.rsplit(".", 1)[1]
            marker = "表格往返验证" if kind == "spreadsheet" else "演示往返验证"
            # An existing file: open, change, save.
            copy = work / f"existing.{extension}"
            shutil.copy2(FIXTURES / fixture, copy)
            content, error = mcp.call("test.open", {"path": str(copy)})
            step(f"{kind}: open {fixture}", not error, document=content.get("document"))
            state = editor(WAIT % 2000 + STATE)
            step(f"{kind}: editable after opening", state and state.get("viewMode") is False, state=state)
            changed = (editor(SHEET_WRITE % ("D2", json.dumps(marker), 1, 3)) if kind == "spreadsheet"
                       else editor(SLIDE_WRITE % ("a.AddSlide(0);", json.dumps(marker))))
            ok = changed and changed.get("modified") and changed.get("viewMode") is False and (
                changed.get("value") == marker if kind == "spreadsheet" else marker in changed.get("texts", []))
            step(f"{kind}: changed in the editor", ok, editor=changed)
            content, error = mcp.call("document.save")
            step(f"{kind}: saved", not error and file_has(copy, marker, prefix), message=content.get("message"))
            # A new one: create, change, save as.
            content, error = mcp.call("document.create", {"kind": kind})
            step(f"{kind}: created", not error, message=content.get("message"))
            fresh = marker + "（新建）"
            state = editor(WAIT % 2500 + STATE)
            step(f"{kind}: new one editable", state and state.get("viewMode") is False, state=state)
            changed = (editor(SHEET_WRITE % ("A1", json.dumps(fresh), 0, 0)) if kind == "spreadsheet"
                       else editor(SLIDE_WRITE % ("", json.dumps(fresh))))
            step(f"{kind}: new one changed", changed and changed.get("modified"), editor=changed)
            target = work / f"new.{extension}"
            content, error = mcp.call("test.saveAs", {"path": str(target)})
            step(f"{kind}: saved as {target.name}", not error and target.is_file() and file_has(target, fresh, prefix),
                 document=content.get("document"))
            # Opened again: the text is read back in the editor.
            content, error = mcp.call("test.open", {"path": str(copy)})
            back = editor(SHEET_READ if kind == "spreadsheet" else SLIDE_READ)
            found = back and (marker in back.get("values", []) if kind == "spreadsheet" else marker in back.get("texts", []))
            step(f"{kind}: reopened and read back", not error and found and back.get("viewMode") is False, editor=back)
            commands(mcp, kind, step, work, prefix)
        state, _ = mcp.call("test.state")
        record["final_state"] = state
    except Exception as error:
        record["error"] = repr(error)
    finally:
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
        record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record["ok"] = "error" not in record and bool(record["steps"]) and all(s["ok"] for s in record["steps"])
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with open(record_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"ok": record["ok"], "record": str(record_path), "error": record.get("error")}, ensure_ascii=False))
    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
