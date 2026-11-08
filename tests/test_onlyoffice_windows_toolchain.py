"""Configuration and command generation only; no upstream build/download runs.

The Windows integration checks execute temporary fixture .cmd files, never the
installed vcvarsall, qmake, CMake, or make programs.
"""

from contextlib import redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream" / "DesktopEditors" / "build_tools"


def load_module(name, path, dependencies):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    with patch.dict(sys.modules, dependencies), patch.object(sys, "path", sys.path[:]):
        spec.loader.exec_module(module)
    return module


class WindowsToolchainTests(unittest.TestCase):
    def setUp(self):
        test_root = ROOT / "tests"
        self.temporary = tempfile.TemporaryDirectory(prefix="oo toolchain ", dir=test_root)
        self.root = Path(self.temporary.name).resolve()
        assert self.root.is_relative_to(test_root.resolve())
        self.addCleanup(self.temporary.cleanup)
        self.base = types.ModuleType("base")
        self.base.platform_is_32 = lambda platform: "_32" in platform
        self.config = load_module("oo_config_test", UPSTREAM / "scripts/config.py", {"base": self.base})
        self.config.options = {
            "vs-path": str(self.root / "Program Files (x86)" / "Visual Studio"),
            "vcvars-ver": "14.44.35207",
            "cmake-dir": str(self.root / "CMake bin"),
            "clean": "0",
            "multiprocess": "0",
            "compiler": "msvc2022",
        }
        self.qmake = load_module("oo_qmake_test", UPSTREAM / "scripts/qmake.py", {
            "base": self.base, "config": self.config, "android_ndk": types.ModuleType("android_ndk"),
        })
        self.environment = patch.dict(os.environ)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def commands(self, **overrides):
        arguments = dict(
            platform="win_64", qmake_app=str(self.root / "Qt kit/qmake.exe"),
            file_pro=str(self.root / "source folder/editor.pro"),
            makefile=str(self.root / "source folder/Makefile.win_64"),
            config_params=["CONFIG+=release build_monolith_objects"],
            qmake_addon=[], jom_exe=str(self.root / "jom bin/jom.exe"),
        )
        arguments.update(overrides)
        return self.qmake.windows_build_commands(**arguments)

    def make_config_fixtures(self):
        for key, filename in (("vs-path", "vcvarsall.bat"), ("cmake-dir", "cmake.exe")):
            directory = Path(self.config.option(key))
            directory.mkdir(parents=True, exist_ok=True)
            (directory / filename).touch()

    def test_configuration_selects_existing_cmake_without_duplicate_path(self):
        self.make_config_fixtures()
        os.environ["PATH"] = "old-tools"
        self.config.configure_windows_toolchain()
        self.config.configure_windows_toolchain()
        self.assertEqual(os.environ["PATH"], self.config.option("cmake-dir") + os.pathsep + "old-tools")
        self.assertEqual(os.environ["OO_CMAKE_DIR"], self.config.option("cmake-dir"))
        self.assertEqual(self.config.check_compiler("win_64")["compiler_64"], "msvc2022_64")

    def test_missing_tools_and_partial_versions_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "vcvarsall"):
            self.config.configure_windows_toolchain()
        self.make_config_fixtures()
        for version in ("14.44", "14.44.35207\n", "14.44.35207&echo unexpected"):
            with self.subTest(version=version):
                self.config.options["vcvars-ver"] = version
                with self.assertRaises(ValueError):
                    self.config.configure_windows_toolchain()
                with self.assertRaises(ValueError):
                    self.commands()
        self.config.options["vcvars-ver"] = "14.44.35207"
        (Path(self.config.option("cmake-dir")) / "cmake.exe").unlink()
        with self.assertRaisesRegex(ValueError, "cmake.exe"):
            self.config.configure_windows_toolchain()

    def test_exact_pin_and_spaced_paths_in_all_commands(self):
        self.config.options["clean"] = "1"
        lines = self.commands(qmake_env_addon="-spec win32-msvc")
        text = "\n".join(lines)
        self.assertIn('"-vcvars_ver=14.44.35207"', lines[2])
        self.assertIn('if not "%VCToolsVersion%"=="14.44.35207" exit /b 1', lines)
        self.assertIn('set "PATH=' + self.config.option("cmake-dir") + ';%PATH%"', lines)
        qmake_lines = [line for line in lines if "qmake.exe" in line]
        self.assertEqual(len(qmake_lines), 2)
        self.assertEqual(qmake_lines[0], qmake_lines[1])
        self.assertIn('"-spec" "win32-msvc"', qmake_lines[0])
        self.assertIn('"' + str(self.root / "source folder/editor.pro") + '"', text)
        for line in lines:
            if "jom.exe" in line:
                self.assertIn('"' + str(self.root / "source folder/Makefile.win_64") + '"', line)
        self.assertEqual(lines[-1], "exit /b %errorlevel%")

    def test_legacy_no_override_and_architecture_selection(self):
        self.config.options["vcvars-ver"] = ""
        self.config.options["cmake-dir"] = ""
        for platform, arch in (("win_64", "x64"), ("win_32", "x86"), ("win_arm64", "x64_arm64")):
            with self.subTest(platform=platform):
                lines = self.commands(platform=platform, jom_exe="")
                self.assertTrue(lines[2].endswith('"' + arch + '"'))
                self.assertNotIn("-vcvars_ver", "\n".join(lines))
                self.assertFalse(any(line.startswith('set "PATH=') for line in lines))
                self.assertTrue(lines[-2].startswith('"nmake.exe" "-f"'))

    def test_unsafe_batch_expansion_is_rejected(self):
        for value in ('path%NAME%', 'line\nbreak', 'embedded"quote'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.commands(file_pro=value)
        with self.assertRaises(ValueError):
            self.qmake.windows_command("unsafe&path/vcvarsall.bat", ["x64"])

    def test_configure_serializes_overrides_only_in_temporary_fixture(self):
        # Compile the configure source with a temporary __file__: config output
        # is isolated; the downloaded source tree is never configured here.
        configure = UPSTREAM / "configure.py"
        arguments = [str(configure), "--update", "0", "--module", "desktop", "--platform", "win_64",
                     "--vs-path", self.config.option("vs-path"), "--vcvars-ver", "14.44.35207",
                     "--cmake-dir", self.config.option("cmake-dir"), "--compiler", "msvc2022"]
        with patch.object(sys, "argv", arguments), redirect_stdout(io.StringIO()):
            exec(compile(configure.read_text(encoding="utf-8"), str(configure), "exec"),
                 {"__file__": str(self.root / "configure.py"), "__name__": "__main__"})
        output = (self.root / "config").read_text(encoding="utf-8")
        self.assertIn('vcvars-ver="14.44.35207"', output)
        self.assertIn('cmake-dir="' + self.config.option("cmake-dir") + '"', output)
        self.assertIn('update="0"', output)

    @unittest.skipUnless(os.name == "nt", "cmd.exe fixture integration needs Windows")
    def test_batch_executes_fixtures_and_propagates_errors(self):
        vs = Path(self.config.option("vs-path"))
        vs.mkdir(parents=True)
        qmake = self.root / "fake qmake.cmd"
        jom = self.root / "fake jom.cmd"
        log = self.root / "commands.log"
        os.environ["OO_TEST_LOG"] = str(log)
        self.config.options["clean"] = "0"
        self.config.options["cmake-dir"] = ""
        for vc_exit, vc_version, qmake_exit, make_exit, expected, stages in (
            (0, "14.44.35207", 0, 0, 0, ["qmake", "make"]),
            (7, "14.44.35207", 0, 0, 7, []),
            (-1073741819, "14.44.35207", 0, 0, 3221225477, []),
            (0, "14.51.36231", 0, 0, 1, []),
            (0, "14.44.35207", 9, 0, 9, ["qmake"]),
            (0, "14.44.35207", -1073741819, 0, 3221225477, ["qmake"]),
            (0, "14.44.35207", 0, 13, 13, ["qmake", "make"]),
        ):
            with self.subTest(vc_exit=vc_exit, vc_version=vc_version, qmake_exit=qmake_exit, make_exit=make_exit):
                log.write_text("", encoding="ascii")
                (vs / "vcvarsall.bat").write_text('@echo off\nset "VCToolsVersion=' + vc_version + '"\nexit /b ' + str(vc_exit) + '\n', encoding="ascii")
                qmake.write_text('@echo off\necho qmake:%~2>>"%OO_TEST_LOG%"\nexit /b ' + str(qmake_exit) + '\n', encoding="ascii")
                jom.write_text('@echo off\necho make:%~2>>"%OO_TEST_LOG%"\nexit /b ' + str(make_exit) + '\n', encoding="ascii")
                script = self.root / "generated commands.cmd"
                script.write_text("\n".join(self.commands(qmake_app=str(qmake), jom_exe=str(jom))), encoding="ascii")
                result = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(script)],
                                        cwd=self.root, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                entries = log.read_text(encoding="ascii").splitlines()
                self.assertEqual([entry.partition(":")[0] for entry in entries], stages)
                if "qmake" in stages:
                    self.assertEqual(entries[0], "qmake:" + str(self.root / "source folder/editor.pro"))
                if "make" in stages:
                    self.assertEqual(entries[-1], "make:" + str(self.root / "source folder/Makefile.win_64"))

    @unittest.skipUnless(os.name == "nt", "cmd.exe fixture integration needs Windows")
    def test_clean_failure_does_not_mask_successful_rebuild(self):
        vs = Path(self.config.option("vs-path"))
        vs.mkdir(parents=True)
        (vs / "vcvarsall.bat").write_text('@echo off\nset "VCToolsVersion=14.44.35207"\nexit /b 0\n', encoding="ascii")
        qmake = self.root / "fake qmake.cmd"
        jom = self.root / "fake jom.cmd"
        log = self.root / "commands.log"
        os.environ["OO_TEST_LOG"] = str(log)
        qmake.write_text('@echo off\necho qmake>>"%OO_TEST_LOG%"\nexit /b 0\n', encoding="ascii")
        jom.write_text('@echo off\necho %~1>>"%OO_TEST_LOG%"\nif "%~1"=="-f" exit /b 0\nexit /b 8\n', encoding="ascii")
        self.config.options["clean"] = "1"
        self.config.options["cmake-dir"] = ""
        script = self.root / "generated commands.cmd"
        script.write_text("\n".join(self.commands(qmake_app=str(qmake), jom_exe=str(jom))), encoding="ascii")
        result = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(script)],
                                cwd=self.root, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(log.read_text(encoding="ascii").splitlines(), ["qmake", "clean", "distclean", "qmake", "-f"])


if __name__ == "__main__":
    unittest.main()
