import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import jsonschema
from tools import gradle, processes, validation


class ValidationOutcomeTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / ".gitignore").write_text("build/\nreports/\n", encoding="utf-8")
        (self.root / "guest").mkdir()
        (self.root / "guest/gradle.properties").write_text(
            "minecraft_version=1.21.4\n", encoding="utf-8"
        )
        for arguments in (
            ["init", "-q"],
            ["add", "."],
            [
                "-c",
                "user.name=Test",
                "-c",
                "user.email=test@example.invalid",
                "commit",
                "-qm",
                "fixture",
            ],
        ):
            subprocess.run(
                ["git", "-C", str(self.root), *arguments], check=True, capture_output=True
            )

    def create(self) -> validation.Validation:
        return validation.Validation(
            self.root,
            "controlled",
            {"timeout": 30, "revision": None, "preflight": False},
            ["bootstrap", "gradle", "evidence"],
        )

    def finish(self, check: validation.Validation) -> int:
        with contextlib.redirect_stdout(io.StringIO()):
            result = check.finish()
        schema_path = Path(__file__).resolve().parents[1] / "schemas/validation-v1.schema.json"
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator(schema).validate(json.loads(check.path.read_text()))
        return result

    def test_source_timeouts_are_not_missing_prerequisites(self) -> None:
        for error in (TimeoutError("expired"), subprocess.TimeoutExpired("git", 1)):
            with self.subTest(error=type(error).__name__):
                check = self.create()
                with patch.object(validation, "source_identity", side_effect=error):
                    with self.assertRaises(validation.Halt):
                        check.preflight(())
                self.assertEqual(check.report["status"], "timed_out")
                self.assertEqual(check.report["preflight"], "timed_out")
                self.assertEqual(self.finish(check), 1)

    def test_probe_failure_status_and_evidence_are_preserved(self) -> None:
        for status in ("failed", "blocked", "timed_out", "interrupted"):
            with self.subTest(status=status):
                check = self.create()
                (check.directory / "probe-java.stdout.log").write_text("", encoding="utf-8")
                (check.directory / "probe-java.stderr.log").write_text(
                    "probe failed\n", encoding="utf-8"
                )
                result = {
                    "status": status,
                    "cleanup": "passed",
                    "exit_code": 17,
                    "elapsed_seconds": 0.1,
                    "reason": "controlled_probe_failure",
                    "stdout": "probe-java.stdout.log",
                    "stderr": "probe-java.stderr.log",
                }
                with (
                    patch.object(validation.shutil, "which", return_value=sys.executable),
                    patch.object(validation, "execute", return_value=result),
                ):
                    with self.assertRaises(validation.Halt):
                        check.preflight(("java",))
                self.assertEqual(check.report["status"], status)
                self.assertEqual(check.report["preflight"], status)
                recorded = check.report["tools"]["java"]
                for key, value in result.items():
                    self.assertIn(key, recorded)
                    self.assertEqual(recorded[key], value)
                self.assertEqual(self.finish(check), 1)
                self.assertEqual(json.loads(check.path.read_text())["tools"]["java"], recorded)

    def test_real_probe_timeout_reports_cleanup(self) -> None:
        check = self.create()
        descriptor_count = len(os.listdir("/proc/self/fd")) if sys.platform == "linux" else None

        def probe(command, cwd, directory, name, timeout):
            return processes.execute(
                [
                    sys.executable,
                    "-S",
                    "-c",
                    "import time; print('ready',flush=True); time.sleep(60)",
                ],
                cwd,
                directory,
                name,
                1.5,
            )

        with (
            patch.object(validation.shutil, "which", return_value=sys.executable),
            patch.object(validation, "execute", side_effect=probe),
        ):
            with self.assertRaises(validation.Halt):
                check.preflight(("java",))
        self.assertEqual(check.report["status"], "timed_out")
        self.assertEqual(check.report["tools"]["java"]["cleanup"], "passed")
        self.assertIn("ready", (check.directory / "probe-java.stdout.log").read_text())
        self.assertEqual(self.finish(check), 1)
        if descriptor_count is not None:
            self.assertEqual(len(os.listdir("/proc/self/fd")), descriptor_count)

    def test_unparseable_probe_version_is_a_failure(self) -> None:
        check = self.create()
        (check.directory / "probe-java.stdout.log").write_text("unexpected\n", encoding="utf-8")
        with (
            patch.object(validation.shutil, "which", return_value=sys.executable),
            patch.object(
                validation,
                "execute",
                return_value={
                    "status": "passed",
                    "cleanup": "passed",
                    "exit_code": 0,
                    "stdout": "probe-java.stdout.log",
                },
            ),
        ):
            with self.assertRaises(validation.Halt):
                check.preflight(("java",))
        self.assertEqual(check.report["status"], "failed")
        self.assertEqual(self.finish(check), 1)

    def gradle_run(self, output: str, arguments=None):
        check = self.create()
        commands = []
        real_run = check.run
        real_preflight = check.preflight

        def stage(name, command, limit=120):
            commands.append((name, command))
            text = json.dumps({"executable": "controlled"}) if name == "bootstrap" else output
            return real_run(name, [sys.executable, "-S", "-c", f"print({text!r})"], limit)

        with (
            patch.object(gradle, "ROOT", self.root),
            patch.object(check, "preflight", side_effect=lambda tools: real_preflight(())),
            patch.object(check, "run", side_effect=stage),
        ):
            try:
                gradle.validate(check, ["build"] if arguments is None else arguments)
            except validation.Halt:
                pass
        code = self.finish(check)
        return check, commands, code

    def test_zero_exit_without_requested_tasks_does_not_pass(self) -> None:
        for output in ("Gradle 8.14.3", "", "> Task :other:build", "> Task :build SKIPPED"):
            with self.subTest(output=output):
                check, _, code = self.gradle_run(output)
                self.assertEqual(code, 1)
                self.assertEqual(check.report["status"], "failed")
                self.assertEqual(check.report["reason"], "requested_task_not_completed")
                self.assertEqual(check.report["uncompleted_tasks"], ["build"])

    def test_failed_task_with_zero_exit_remains_failed(self) -> None:
        check, _, code = self.gradle_run("> Task :compileJava FAILED\n> Task :build")
        self.assertEqual(code, 1)
        self.assertEqual(check.report["reason"], "gradle_task_failed")
        self.assertEqual(check.report["gradle_failed_tasks"], [":compileJava"])

    def test_completed_and_up_to_date_builds_remain_valid(self) -> None:
        for suffix in ("", " UP-TO-DATE", " FROM-CACHE"):
            with self.subTest(suffix=suffix):
                check, _, code = self.gradle_run("> Task :build" + suffix)
                self.assertEqual(code, 0)
                self.assertEqual(check.report["status"], "passed")

    def test_nonexecuting_and_redirecting_options_are_rejected_before_preflight(self) -> None:
        for option in (
            "--help",
            "-h",
            "--version",
            "-v",
            "--stop",
            "--status",
            "--scan",
            "--init-s=external.gradle",
            "--project-d=external",
            "--dry-r",
            "--system-prop=org.gradle.project.runMinecraftTests=true",
            "-Dorg.gradle.project.runMinecraftTests=true",
            "--project-prop=runMinecraftTests=true",
            "-PrunMinecraftTests",
            "--quiet",
            "--console=rich",
        ):
            with self.subTest(option=option):
                check = self.create()
                with patch.object(
                    check, "preflight", side_effect=AssertionError("preflight must not run")
                ) as preflight:
                    with self.assertRaises(validation.Halt):
                        gradle.validate(check, ["build", option])
                    preflight.assert_not_called()
                self.assertEqual(self.finish(check), 1)

    def test_task_paths_are_not_collapsed_to_other_projects(self) -> None:
        for task in ("other:build", ":other:runGameTest"):
            with self.subTest(task=task):
                check = self.create()
                with patch.object(
                    check, "preflight", side_effect=AssertionError("preflight must not run")
                ) as preflight:
                    with self.assertRaises(validation.Halt):
                        gradle.validate(check, [task])
                    preflight.assert_not_called()
                self.assertEqual(self.finish(check), 1)

    def test_conflicting_opt_ins_are_rejected(self) -> None:
        for name in ("runMinecraftTests", "acceptMinecraftEula"):
            with self.subTest(name=name):
                check = self.create()
                with patch.object(
                    check, "preflight", side_effect=AssertionError("preflight must not run")
                ) as preflight:
                    with self.assertRaises(validation.Halt):
                        gradle.validate(check, ["build", f"-P{name}=true", f"-P{name}=false"])
                    preflight.assert_not_called()
                self.assertEqual(self.finish(check), 1)

    def test_default_build_pins_game_flags_despite_ambient_properties(self) -> None:
        with patch.dict(
            os.environ,
            {
                "ORG_GRADLE_PROJECT_runMinecraftTests": "true",
                "ORG_GRADLE_PROJECT_acceptMinecraftEula": "true",
            },
        ):
            check, commands, code = self.gradle_run("> Task :build")
        self.assertEqual(code, 0)
        self.assertFalse(check.report["minecraft_execution"])
        command = dict(commands)["gradle"]
        self.assertEqual(command.count("-PrunMinecraftTests=false"), 1)
        self.assertEqual(command.count("-PacceptMinecraftEula=false"), 1)

    def test_explicit_server_opt_in_and_safe_flags_are_preserved(self) -> None:
        arguments = [
            "runGameTest",
            "-PrunMinecraftTests=true",
            "-PacceptMinecraftEula=true",
            "--offline",
            "--stacktrace",
            "--max-workers=2",
        ]
        check, commands, code = self.gradle_run("> Task :runGameTest", arguments)
        self.assertEqual(code, 0)
        self.assertTrue(check.report["minecraft_execution"])
        command = dict(commands)["gradle"]
        for option in arguments[1:]:
            self.assertEqual(command.count(option), 1)

    def test_game_tasks_cannot_use_cached_results(self) -> None:
        for suffix in (" UP-TO-DATE", " FROM-CACHE", " SKIPPED", " NO-SOURCE"):
            with self.subTest(suffix=suffix):
                check, _, code = self.gradle_run(
                    "> Task :runGameTest" + suffix,
                    ["runGameTest", "-PrunMinecraftTests=true", "-PacceptMinecraftEula=true"],
                )
                self.assertEqual(code, 1)
                self.assertEqual(check.report["reason"], "game_task_not_executed")

    def test_interrupted_preflight_has_no_running_state(self) -> None:
        def interrupted(check, arguments):
            check.report["preflight"] = "running"
            raise KeyboardInterrupt

        with contextlib.redirect_stdout(io.StringIO()):
            code = validation.entry(self.root, "controlled", [], 30, [], interrupted)
        self.assertEqual(code, 1)
        report = next((self.root / "reports/validation").glob("*/result.json"))
        data = json.loads(report.read_text())
        self.assertEqual(data["status"], "interrupted")
        self.assertEqual(data["preflight"], "interrupted")

    def test_unexpected_preflight_error_has_no_running_state(self) -> None:
        def broken(check, arguments):
            check.report["preflight"] = "running"
            raise ValueError("controlled failure")

        with contextlib.redirect_stdout(io.StringIO()):
            code = validation.entry(self.root, "controlled", [], 30, [], broken)
        self.assertEqual(code, 1)
        report = next((self.root / "reports/validation").glob("*/result.json"))
        data = json.loads(report.read_text())
        self.assertEqual(data["status"], "failed")
        self.assertEqual(data["preflight"], "failed")
        self.assertTrue((report.parent / data["diagnostic"]).is_file())
