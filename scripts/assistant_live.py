#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test the assistant on the real editor through the program's MCP server.

Starts Open Office with --mcp-port 0 --mcp-test and a new profile, copies the scenario's
document so the original is never changed, then calls MCP tools step by step: the editor
commands (document.outline, table.read, table.setCell, format.cells, ...) and the test tools
that drive the assistant like a person (test.ask, test.press, test.quick, test.state).
Each step can state what it expects; every call and answer is kept. At the end the program
(and the editor and AI engine it started) is ended, and a record goes to
evidence/assistant-live/<label>.json.

Scenario file (JSON):
    {"name": "...", "document": "fixtures/assistant-samples/x.docx",
     "steps": [{"tool": "table.read", "args": {"table": 1}, "expect": {"ok": true}}, ...]}
Expectations: ok (true/false), code, saidIncludes (text in what the assistant said),
buttonsInclude (button titles), valueIncludes (text in the JSON of the tool's value).
A step may also be {"check": "cell", "table": 1, "row": 2, "column": 3, "equals": "..."} or
{"check": "unchangedExcept", "table": 1, "cells": ["R2C3"]} (compares with the table as it was
when the scenario started).

Usage:
    python -I scripts/assistant_live.py <label> --runtime build/full-source-run-builtin-ai-rN <scenario.json> [...]
    python -I scripts/assistant_live.py <label> --exe build/full-source-host/bin/OpenOffice.exe --runtime <runtime> <scenario.json>
"""

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "assistant-live"
EVIDENCE = ROOT / "evidence" / "assistant-live"
MODELS = ROOT / "dependencies" / "local-models" / "download"
ENGINE = ROOT / "dependencies" / "llama.cpp-b12202"


class Mcp:
    def __init__(self, endpoint):
        self.url = endpoint["url"]
        self.token = endpoint["token"]
        self.next_id = 0
        # Loopback only: never through a proxy.
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, method, params=None, timeout=900):
        self.next_id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}}).encode("utf-8")
        request = urllib.request.Request(self.url, data=body, method="POST", headers={
            "Content-Type": "application/json", "Authorization": "Bearer " + self.token})
        with self.opener.open(request, timeout=timeout) as response:
            answer = json.loads(response.read().decode("utf-8"))
        if "error" in answer:
            raise RuntimeError(f"{method}: {answer['error']}")
        return answer["result"]

    def call(self, tool, arguments=None, timeout=None):
        arguments = arguments or {}
        if timeout is None:
            # The server answers every command within 120 s and every test tool within its
            # timeoutSeconds (default 240); a little more here, so the server's answer wins.
            timeout = arguments.get("timeoutSeconds", 240) + 60 if tool.startswith("test.") else 180
        result = self.request("tools/call", {"name": tool, "arguments": arguments}, timeout)
        return result.get("structuredContent", {}), bool(result.get("isError"))


def table_cells(mcp, table):
    content, error = mcp.call("table.read", {"table": table})
    return None if error else content.get("value", {}).get("rows", [])


def table_grid(mcp, table):
    cells = table_cells(mcp, table)
    return None if cells is None else [[cell.get("text", "") for cell in row] for row in cells]


def cell_range(spec):
    """R1C1-R1C4 (or one cell R2C3) as 0-based (row, column) pairs."""
    match = re.fullmatch(r"R(\d+)C(\d+)(?:-R(\d+)C(\d+))?", spec)
    r1, c1 = int(match.group(1)), int(match.group(2))
    r2, c2 = (int(match.group(3)), int(match.group(4))) if match.group(3) else (r1, c1)
    return [(r - 1, c - 1) for r in range(r1, r2 + 1) for c in range(c1, c2 + 1)]


def cell_name(r, c):
    return f"R{r + 1}C{c + 1}"


def check_expectation(expect, content, error):
    problems = []
    if "ok" in expect and expect["ok"] != (not error):
        problems.append(f"expected ok={expect['ok']}, got isError={error}")
    if "code" in expect and content.get("code") != expect["code"]:
        problems.append(f"expected code {expect['code']}, got {content.get('code')}")
    said = "\n".join(item.get("text", "") for item in content.get("said", []))
    for text in expect.get("saidIncludes", []):
        if text not in said:
            problems.append(f"the assistant did not say {text!r}")
    for title in expect.get("buttonsInclude", []):
        if title not in content.get("buttons", []):
            problems.append(f"no button {title!r} (buttons: {content.get('buttons')})")
    value = json.dumps(content.get("value", {}), ensure_ascii=False, separators=(",", ":"))
    for text in expect.get("valueIncludes", []):
        if text not in value:
            problems.append(f"value has no {text!r}")
    return problems


def run_scenario(mcp, scenario, work, number):
    record = {"name": scenario.get("name"), "steps": [], "ok": True}
    document = ROOT / scenario["document"]
    copy = work / f"{number:02d}-{document.name}"  # one copy per scenario: an open file is locked
    shutil.copy2(document, copy)
    content, error = mcp.call("test.open", {"path": str(copy)}, timeout=180)
    record["open"] = {"ok": not error, "document": str(copy)}
    if error:
        record["ok"] = False
        record["error"] = content
        return record
    before = {}
    for step in scenario.get("steps", []):
        started = time.time()
        entry = {"step": step}
        try:
            entry["problems"] = run_step(mcp, step, entry, before)
            failed = False
        except Exception as error:  # kept in the record; the rest of this scenario is skipped
            entry["problems"] = [f"step failed: {error!r}"]
            failed = True
        entry["seconds"] = round(time.time() - started, 2)
        record["steps"].append(entry)
        if entry["problems"]:
            record["ok"] = False
        if failed:
            break
    # Saved (the copy, never the original), so the next document opens without a prompt
    # and the result stays as evidence.
    try:
        saved, error = mcp.call("document.save")
        record["saved"] = {"ok": not error, "file": str(copy), "message": saved.get("message")}
    except Exception as error:
        record["saved"] = {"ok": False, "file": str(copy), "message": repr(error)}
        record["ok"] = False
    return record


def run_step(mcp, step, entry, before):
    """One step; returns its problems (empty when it went as expected)."""
    if "check" not in step:
        content, error = mcp.call(step["tool"], step.get("args", {}))
        entry["result"] = content
        entry["isError"] = error
        return check_expectation(step.get("expect", {}), content, error)
    table = step.get("table", 1)
    grid = table_grid(mcp, table)
    problems = []
    if grid is None:
        return [f"table {table} cannot be read"]
    if step["check"] == "cell":
        actual = grid[step["row"] - 1][step["column"] - 1]
        entry["actual"] = actual
        if "equals" in step and actual != step["equals"]:
            problems.append(f"R{step['row']}C{step['column']} is {actual!r}, expected {step['equals']!r}")
        if "contains" in step and step["contains"] not in actual:
            problems.append(f"R{step['row']}C{step['column']} {actual!r} has no {step['contains']!r}")
        if "matches" in step and not re.fullmatch(step["matches"], actual):
            problems.append(f"R{step['row']}C{step['column']} {actual!r} does not match {step['matches']!r}")
    elif step["check"] == "format":
        cells = table_cells(mcp, table)
        for r, c in cell_range(step["cells"]):
            cell = cells[r][c] if r < len(cells) and c < len(cells[r]) else None
            name = cell_name(r, c)
            if cell is None:
                problems.append(f"{name} does not exist")
                continue
            if "bold" in step and bool(cell.get("bold")) != step["bold"]:
                problems.append(f"{name} bold is {bool(cell.get('bold'))}")
            if "align" in step and cell.get("align", "left") != step["align"]:
                problems.append(f"{name} align is {cell.get('align', 'left')}")
            if step.get("filled") is True and not cell.get("fill"):
                problems.append(f"{name} has no fill")
            if step.get("filled") is False and cell.get("fill"):
                problems.append(f"{name} has fill {cell.get('fill')}")
    elif step["check"] == "snapshot":
        before[table] = grid
    elif step["check"] == "unchangedExcept":
        allowed = set(step.get("cells", []))
        old = before.get(table) or []
        changed = [cell_name(r, c) for r, row in enumerate(grid) for c, text in enumerate(row)
                   if r < len(old) and c < len(old[r]) and old[r][c] != text]
        entry["changed"] = changed
        stray = [name for name in changed if name not in allowed]
        if stray:
            problems.append(f"cells changed that should not: {stray}")
        if step.get("someChanged") and not changed:
            problems.append("nothing changed")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("label")
    parser.add_argument("scenarios", nargs="+", type=Path)
    parser.add_argument("--runtime", type=Path, required=True, help="runtime folder (editor, CEF, Qt, ai-engine)")
    parser.add_argument("--exe", type=Path, help="another OpenOffice.exe to run with that runtime (a fresh build)")
    parser.add_argument("--model", help="built-in model id (qwen3-1.7b, qwen3-4b...); default: the one chosen in the settings")
    args = parser.parse_args()
    runtime = (args.runtime if args.runtime.is_absolute() else ROOT / args.runtime).resolve()
    exe = (args.exe if args.exe and args.exe.is_absolute() else ROOT / args.exe).resolve() if args.exe else runtime / "OpenOffice.exe"
    work = WORK / args.label
    record_path = EVIDENCE / f"{args.label}.json"
    if work.exists() or record_path.exists():
        sys.exit(f"{work} or {record_path} exists; pick a new label")
    work.mkdir(parents=True)
    profile = work / "profile"
    environment = dict(os.environ)
    environment["PATH"] = os.pathsep.join([str(runtime), str(Path(os.environ["SystemRoot"]) / "System32")])
    environment["OPEN_OFFICE_MODEL_DIR"] = str(MODELS)
    if args.model:
        environment["OPEN_OFFICE_AI_MODEL"] = args.model  # the settings stay as they are
    if not (exe.parent / "ai-engine").is_dir():
        environment["OPEN_OFFICE_AI_ENGINE"] = str(runtime / "ai-engine") if (runtime / "ai-engine").is_dir() else str(ENGINE)
    for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "OPEN_OFFICE_AI_BACKEND", "OPEN_OFFICE_AI_ENGINE_URL"):
        environment.pop(name, None)
    command = [str(exe), "--engine-profile", str(profile), "--mcp-port", "0", "--mcp-test"]
    if exe.parent != runtime:
        command += ["--onlyoffice-runtime", str(runtime)]
    started = datetime.datetime.now(datetime.timezone.utc)
    with open(work / "stdout.log", "wb") as out, open(work / "stderr.log", "wb") as err:
        process = subprocess.Popen(command, cwd=runtime, env=environment, stdout=out, stderr=err)
    record = {"label": args.label, "started_utc": started.isoformat(), "command": command, "runtime": str(runtime),
              "model": args.model or "(settings)",
              "exe": str(exe), "scenarios": []}
    try:
        endpoint_path = profile / "mcp-endpoint.json"
        deadline = time.time() + 60
        while not endpoint_path.is_file() and time.time() < deadline and process.poll() is None:
            time.sleep(0.5)
        if not endpoint_path.is_file():
            raise RuntimeError(f"no MCP endpoint (exit code {process.poll()})")
        mcp = Mcp(json.loads(endpoint_path.read_text(encoding="utf-8")))
        record["initialize"] = mcp.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                                          "clientInfo": {"name": "assistant_live", "version": "1"}})
        record["tools"] = [tool["name"] for tool in mcp.request("tools/list")["tools"]]
        for number, path in enumerate(args.scenarios, 1):
            scenario = json.loads((path if path.is_absolute() else ROOT / path).read_text(encoding="utf-8"))
            try:
                result = run_scenario(mcp, scenario, work, number)
            except Exception as error:  # this scenario failed to start; the next one still runs
                result = {"name": scenario.get("name"), "steps": [], "ok": False, "error": repr(error)}
            result["file"] = str(path)
            record["scenarios"].append(result)
            print(json.dumps({"scenario": result["name"], "ok": result["ok"],
                              "problems": [p for s in result["steps"] for p in s["problems"]][:6]}, ensure_ascii=False))
        state, _ = mcp.call("test.state")
        record["final_state"] = state
    except Exception as error:  # the record still says what happened
        record["error"] = repr(error)
    finally:
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
        record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record["ok"] = "error" not in record and all(s["ok"] for s in record["scenarios"])
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with open(record_path, "x", encoding="utf-8", newline="\n") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps({"ok": record["ok"], "record": str(record_path), "error": record.get("error")}, ensure_ascii=False))
    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
