"""Behavioral tests for the OOXML smoke-test harness, not an editor test."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import warnings
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile


MODULE_PATH = Path(__file__).resolve().parents[1] / "scripts" / "roundtrip.py"
SPEC = importlib.util.spec_from_file_location("roundtrip", MODULE_PATH)
assert SPEC and SPEC.loader
rt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rt)


class RoundtripTests(unittest.TestCase):
    def setUp(self):
        # Use the authorized workspace; Windows sandbox TEMP can create a
        # directory yet deny access to its children. Cleanup is limited to the
        # freshly allocated path under this test directory.
        test_root = Path(__file__).resolve().parent
        self.temp = tempfile.TemporaryDirectory(prefix="roundtrip-", dir=test_root)
        assert Path(self.temp.name).resolve().is_relative_to(test_root)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.inputs = self.base / "inputs"
        self.outputs = self.base / "outputs"
        self.generated = rt.generate(self.inputs)

    def copy_outputs(self):
        self.outputs.mkdir()
        for name in rt.FIXTURES:
            shutil.copyfile(self.inputs / name, self.outputs / name)

    def edit_part(self, filename, part, transform):
        path = self.outputs / filename
        with ZipFile(path) as archive:
            parts = {name: archive.read(name) for name in archive.namelist()}
        parts[part] = transform(parts[part])
        with ZipFile(path, "w", ZIP_DEFLATED) as archive:
            for name, content in parts.items():
                archive.writestr(name, content)

    def evidence(self):
        path = self.base / "operator-evidence.json"
        path.write_text(json.dumps({
            "engine": "ONLYOFFICE Desktop Editors", "version": "test declaration",
            "operation": "open-save-reopen", "saved_files": list(rt.FIXTURES),
            "run_note": "Unit-test declaration only; no actual application was run.",
        }), encoding="utf-8")
        return path

    def test_generated_packages_have_expected_semantics(self):
        self.assertFalse(self.generated["engine_executed"])
        word = rt.snapshot(self.inputs / rt.FIXTURES[0])
        self.assertIn("Open Office 兼容性样本", word["paragraphs"])
        self.assertEqual(word["tables"][0][1], ["办公套件", "3"])
        sheet = rt.snapshot(self.inputs / rt.FIXTURES[1])
        self.assertEqual(sheet["sheets"][0]["cells"]["C2"], {"formula": "SUM(A2:B2)"})
        slides = rt.snapshot(self.inputs / rt.FIXTURES[2])
        self.assertEqual(slides["slides"][0]["preset_shapes"], {"rect": 1, "roundRect": 1})

    def test_generation_never_overwrites_existing_files(self):
        before = {name: (self.inputs / name).read_bytes() for name in rt.FIXTURES}
        with self.assertRaises(FileExistsError):
            rt.generate(self.inputs)
        self.assertEqual(before, {name: (self.inputs / name).read_bytes() for name in rt.FIXTURES})

    def test_missing_outputs_are_not_executed(self):
        report, code = rt.compare(self.inputs, self.outputs)
        self.assertEqual(code, 2)
        self.assertEqual(report["status"], "not_executed")
        self.assertTrue(all(item["status"] == "not_executed" for item in report["results"]))

    def test_missing_input_fails_instead_of_passing_empty_corpus(self):
        report, code = rt.compare(self.base / "missing", self.outputs)
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")

    def test_input_cannot_be_its_own_output(self):
        report, code = rt.compare(self.inputs, self.inputs)
        self.assertEqual(code, 1)
        self.assertTrue(all(item["status"] == "invalid_output" for item in report["results"]))

    def test_copied_files_do_not_prove_editor_execution(self):
        self.copy_outputs()
        report, code = rt.compare(self.inputs, self.outputs)
        self.assertEqual(code, 2)
        self.assertEqual(report["status"], "semantic_match_unverified_provenance")
        self.assertFalse(report["runtime_evidence"]["engine_execution_verified"])

    def test_operator_declaration_does_not_claim_visual_or_build_success(self):
        self.copy_outputs()
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "semantic_checks_passed_with_operator_evidence")
        self.assertEqual(report["runtime_evidence"]["status"], "operator_declared")
        self.assertFalse(report["runtime_evidence"]["engine_execution_verified"])
        self.assertEqual(report["visual_fidelity"], "not_tested")
        self.assertEqual(report["source_build"], "not_tested")

    def test_bad_evidence_does_not_allow_pass(self):
        self.copy_outputs()
        evidence = self.base / "bad.json"
        evidence.write_text("{}", encoding="utf-8")
        report, code = rt.compare(self.inputs, self.outputs, evidence)
        self.assertEqual(code, 2)
        self.assertEqual(report["runtime_evidence"]["status"], "invalid")

    def test_text_loss_is_detected(self):
        self.copy_outputs()
        self.edit_part(rt.FIXTURES[0], "word/document.xml", lambda data: data.replace("办公套件".encode(), "软件".encode()))
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 1)
        self.assertEqual(report["results"][0]["status"], "semantic_mismatch")
        self.assertTrue(report["results"][0]["differences"])

    def test_formula_replaced_by_cached_value_is_detected(self):
        self.copy_outputs()
        self.edit_part(rt.FIXTURES[1], "xl/worksheets/sheet1.xml", lambda data: data.replace(b"<f>SUM(A2:B2)</f>", b""))
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 1)
        self.assertEqual(report["results"][1]["status"], "semantic_mismatch")

    def test_formula_cache_and_numeric_serialization_do_not_false_fail(self):
        self.copy_outputs()
        self.edit_part(rt.FIXTURES[1], "xl/worksheets/sheet1.xml", lambda data: data.replace(b"<v>2</v>", b"<v>2.000</v>").replace(b"<f>SUM(A2:B2)</f><v>5</v>", b"<f>SUM(A2:B2)</f><v>5.000</v>"))
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 0)
        self.assertEqual(report["results"][1]["status"], "semantic_match")

    def test_text_run_splitting_and_namespace_prefixes_do_not_false_fail(self):
        self.copy_outputs()

        def split(data):
            root = ET.fromstring(data)
            paragraph = root.find(".//w:p", rt.NS)
            run = paragraph.find("w:r", rt.NS)
            text = run.find("w:t", rt.NS)
            original = text.text
            text.text = original[:5]
            second = ET.SubElement(paragraph, f"{{{rt.W}}}r")
            ET.SubElement(second, f"{{{rt.W}}}t").text = original[5:]
            return ET.tostring(root, encoding="utf-8", xml_declaration=True)

        self.edit_part(rt.FIXTURES[0], "word/document.xml", split)
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 0)

    def test_shape_change_is_detected(self):
        self.copy_outputs()
        self.edit_part(rt.FIXTURES[2], "ppt/slides/slide1.xml", lambda data: data.replace(b'prst="roundRect"', b'prst="ellipse"'))
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 1)
        self.assertEqual(report["results"][2]["status"], "semantic_mismatch")

    def test_corrupt_zip_is_detected(self):
        self.copy_outputs()
        (self.outputs / rt.FIXTURES[0]).write_bytes(b"not a ZIP archive")
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 1)
        self.assertEqual(report["results"][0]["status"], "invalid_package")

    def test_broken_internal_relationship_is_detected(self):
        self.copy_outputs()
        self.edit_part(rt.FIXTURES[2], "ppt/slides/_rels/slide1.xml.rels", lambda data: data.replace(b"slideLayout1.xml", b"absent.xml"))
        report, code = rt.compare(self.inputs, self.outputs, self.evidence())
        self.assertEqual(code, 1)
        self.assertEqual(report["results"][2]["status"], "invalid_package")

    def test_duplicate_zip_entries_are_rejected(self):
        self.copy_outputs()
        path = self.outputs / rt.FIXTURES[0]
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with ZipFile(path, "a") as archive:
                archive.writestr("word/document.xml", rt.document_parts()["word/document.xml"])
        with self.assertRaisesRegex(rt.PackageError, "Duplicate ZIP"):
            rt.snapshot(path)


if __name__ == "__main__":
    unittest.main()
