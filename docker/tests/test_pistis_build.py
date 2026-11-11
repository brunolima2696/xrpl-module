"""Offline tests of orchestration; compiler and Conan commands are stubbed."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "build-pistis.sh"


class BuildScriptTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pistis-build-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "docker").mkdir()
        (self.root / "conan").mkdir()
        (self.root / ".build").mkdir()
        (self.root / "fakebin").mkdir()
        shutil.copyfile(SCRIPT, self.root / "docker/build-pistis.sh")
        self.log = self.root / "commands.log"
        self.env = {
            **os.environ,
            "PATH": str(self.root / "fakebin") + os.pathsep + os.environ["PATH"],
            "COMMAND_LOG": str(self.log),
            "BUILD_TYPE": "Release",
            "BUILD_JOBS": "2",
            "TEST_SUITES": "LoanSet,Vault,Feature,ServerDefinitions",
        }
        self.write_script("conan/init.sh", 'echo init >>"$COMMAND_LOG"\n')
        self.write_script(
            "fakebin/conan",
            'echo "conan $*" >>"$COMMAND_LOG"\nexit "${CONAN_RESULT:-0}"\n',
        )
        self.write_script(
            "fakebin/cmake",
            'echo "cmake $*" >>"$COMMAND_LOG"\nexit "${CMAKE_RESULT:-0}"\n',
        )
        self.write_script(
            ".build/xrpld",
            'echo "xrpld $*" >>"$COMMAND_LOG"\nexit "${TEST_RESULT:-0}"\n',
        )

    def write_script(self, name, content):
        path = self.root / name
        path.write_text("#!/usr/bin/env bash\n" + content, encoding="utf-8")
        path.chmod(0o755)

    def run_build(self, stage="image", **env):
        result = subprocess.run(
            ["bash", str(self.root / "docker/build-pistis.sh"), stage],
            env={**self.env, **env}, capture_output=True, text=True, check=False,
        )
        commands = self.log.read_text() if self.log.exists() else ""
        return result.returncode, commands

    def test_dependencies_do_not_compile_server(self):
        code, commands = self.run_build("dependencies")
        self.assertEqual(code, 0)
        self.assertIn("--settings build_type=Release", commands)
        self.assertIn("--options:host=&:tests=True", commands)
        self.assertIn("--options:host=&:xrpld=True", commands)
        self.assertNotIn("cmake", commands)
        self.assertNotIn("xrpld --unittest", commands)

    def test_image_builds_and_tests(self):
        code, commands = self.run_build()
        self.assertEqual(code, 0)
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", commands)
        self.assertIn("--build . --target xrpld --parallel 2", commands)
        self.assertIn("xrpld --unittest=LoanSet,Vault,Feature,ServerDefinitions", commands)

    def test_dependency_failure_stops_before_cmake(self):
        code, commands = self.run_build(CONAN_RESULT="7")
        self.assertEqual(code, 7)
        self.assertNotIn("cmake", commands)

    def test_cmake_failure_stops_before_tests(self):
        code, commands = self.run_build(CMAKE_RESULT="8")
        self.assertEqual(code, 8)
        self.assertNotIn("xrpld --unittest", commands)

    def test_failed_tests_fail_build_despite_tee(self):
        code, commands = self.run_build(TEST_RESULT="9")
        self.assertEqual(code, 9)
        self.assertIn("xrpld --unittest", commands)

    def test_invalid_stage(self):
        self.assertEqual(self.run_build("unknown"), (2, ""))

    def test_invalid_parallelism(self):
        self.assertEqual(self.run_build(BUILD_JOBS="0"), (2, ""))

    def test_invalid_build_type(self):
        self.assertEqual(self.run_build(BUILD_TYPE="Other"), (2, ""))

    def test_tests_cannot_be_disabled_with_empty_selection(self):
        self.assertEqual(self.run_build(TEST_SUITES=""), (2, ""))


if __name__ == "__main__":
    unittest.main()
