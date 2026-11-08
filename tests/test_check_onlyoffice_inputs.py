"""Local-input checker tests; never download or execute ONLYOFFICE code."""

from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[1] / "scripts" / "check_onlyoffice_inputs.py"
SPEC = importlib.util.spec_from_file_location("check_onlyoffice_inputs", MODULE_PATH)
assert SPEC and SPEC.loader
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class OnlyofficeInputTests(unittest.TestCase):
    def setUp(self):
        test_root = Path(__file__).resolve().parent
        self.temporary = tempfile.TemporaryDirectory(prefix="onlyoffice-inputs-", dir=test_root)
        self.root = Path(self.temporary.name).resolve()
        assert self.root.is_relative_to(test_root)
        self.addCleanup(self.temporary.cleanup)
        self.lock = self.root / "sources.lock.json"
        self.lock.write_text(json.dumps({
            "desktop_editors": {"commit": "a" * 40},
            "build_tools": {"commit": "b" * 40},
        }), encoding="utf-8")

    def test_missing_roots_report_exact_paths_and_exit_two(self):
        source = self.root / "missing-source"
        build = self.root / "missing-build-tools"
        with patch.object(checker, "git_read") as git:
            report, code = checker.check_inputs(source, build, lock_file=self.lock)
        self.assertEqual(code, 2)
        self.assertEqual({item["path"] for item in report["issues"]}, {str(source), str(build)})
        self.assertTrue(all(item["code"] == "missing_root" for item in report["issues"]))
        self.assertEqual(report["roots"]["runtime"]["status"], "not_requested")
        self.assertFalse(report["upstream_code_executed"])
        self.assertEqual(report["integration_status"], "not_verified")
        git.assert_not_called()

    def test_plain_directory_is_not_treated_as_a_parent_git_repository(self):
        source = self.root / "source"
        build = self.root / "build"
        for kind, directory in (("desktop_editors", source), ("build_tools", build)):
            directory.mkdir()
            for name in checker.MARKERS[kind]:
                (directory / name).write_text("fixture marker", encoding="utf-8")
        with patch.object(checker, "git_read") as git:
            report, code = checker.check_inputs(source, build, lock_file=self.lock)
        self.assertEqual(code, 2)
        self.assertEqual([item["code"] for item in report["issues"]], ["not_a_local_repository"] * 2)
        git.assert_not_called()

    def test_existing_output_is_preserved_without_checking_sources(self):
        output = self.root / "existing.json"
        output.write_text("user content", encoding="utf-8")
        captured = io.StringIO()
        with patch.object(checker, "check_inputs") as check, redirect_stdout(captured):
            code = checker.main(["--output", str(output)])
        self.assertEqual(code, 2)
        self.assertEqual(output.read_text(encoding="utf-8"), "user content")
        self.assertEqual(json.loads(captured.getvalue())["status"], "output_refused")
        check.assert_not_called()

    def test_runtime_presence_never_claims_execution_or_integration(self):
        runtime = self.root / "runtime"
        runtime.mkdir()
        (runtime / "DesktopEditors.exe").write_bytes(b"filename-only fixture; not executable")
        (runtime / "libcef.dll").write_bytes(b"filename-only fixture; not a DLL")
        (runtime / "sdkjs").mkdir()
        (runtime / "sdkjs" / "sdk.js").write_text("do not execute", encoding="utf-8")
        report, code = checker.check_inputs(self.root / "absent-source", self.root / "absent-build",
                                           runtime, lock_file=self.lock)
        presence = report["roots"]["runtime"]["presence"]
        self.assertEqual(presence["categories"]["exe"]["count"], 1)
        self.assertEqual(presence["categories"]["dll"]["count"], 1)
        self.assertEqual(presence["categories"]["editor_resource_candidates"]["count"], 1)
        self.assertFalse(report["upstream_code_executed"])
        self.assertEqual(report["integration_status"], "not_verified")
        self.assertEqual(report["roundtrip_status"], "not_executed")
        self.assertEqual(code, 2)

    def test_revision_mismatch_and_uninitialized_submodule_are_reported(self):
        source = self.root / "source"
        source.mkdir()
        (source / ".git").mkdir()
        for name in checker.MARKERS["desktop_editors"]:
            (source / name).write_text("fixture marker", encoding="utf-8")
        results = [
            {"returncode": 0, "stdout": str(source)},
            {"returncode": 0, "stdout": "c" * 40},
            {"returncode": 0, "stdout": "-" + "d" * 40 + " sdkjs"},
        ]
        with patch.object(checker, "git_read", side_effect=results):
            report, issues = checker.inspect_root(source, "desktop_editors", "a" * 40,
                                                  {"sdkjs": {"commit": "d" * 40}})
        self.assertFalse(report["revision_verified"])
        self.assertEqual({item["code"] for item in issues}, {"commit_mismatch", "submodule_uninitialized"})
        self.assertEqual(report["submodules"][0]["status"], "uninitialized")


if __name__ == "__main__":
    unittest.main()
