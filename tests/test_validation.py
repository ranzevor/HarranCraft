import contextlib
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import gradle, processes, test_bridge, validation


class SourceFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        (self.root / ".gitignore").write_text("build/\nreports/\n", encoding="utf-8")
        (self.root / "source.txt").write_text("source\n", encoding="utf-8")
        for args in (
            ["init", "-q"],
            ["add", "."],
            [
                "-c",
                "user.name=Test",
                "-c",
                "user.email=test@example.invalid",
                "commit",
                "-qm",
                "test: add source fixture",
            ],
        ):
            subprocess.run(["git", "-C", str(self.root), *args], check=True, capture_output=True)

    def create(self, names=None, **settings) -> validation.Validation:
        return validation.Validation(
            self.root,
            "controlled",
            {
                "timeout": 30,
                "revision": None,
                "preflight": False,
                **settings,
            },
            ["first", "second"] if names is None else names,
        )

    def finish(self, check: validation.Validation) -> int:
        with contextlib.redirect_stdout(io.StringIO()):
            return check.finish()


class SourceAndReportTests(SourceFixture):
    def test_source_identity_tracks_dirty_untracked_and_deleted_content(self) -> None:
        first = validation.source_identity(self.root)
        self.assertFalse(first["dirty"])
        self.assertEqual(first["file_count"], 2)
        (self.root / "source.txt").write_text("changed\n", encoding="utf-8")
        second = validation.source_identity(self.root)
        self.assertTrue(second["dirty"])
        self.assertNotEqual(first["snapshot_sha256"], second["snapshot_sha256"])
        (self.root / "new.txt").write_text("new\n", encoding="utf-8")
        third = validation.source_identity(self.root)
        self.assertEqual(third["file_count"], 3)
        (self.root / "source.txt").unlink()
        self.assertNotEqual(
            third["snapshot_sha256"], validation.source_identity(self.root)["snapshot_sha256"]
        )

    def test_unsupported_python_is_rejected_before_source_checks(self) -> None:
        check = self.create()
        check.report["platform"]["python"] = "3.11.9"
        with patch.object(validation, "source_identity") as identity:
            with self.assertRaises(validation.Halt):
                check.preflight(())
            identity.assert_not_called()
        self.assertEqual(check.report["reason"], "python_version_unsupported")
        self.assertEqual(self.finish(check), 1)

    def test_nested_directory_is_not_mislabeled_as_repository(self) -> None:
        nested = self.root / "nested"
        nested.mkdir()
        with self.assertRaisesRegex(ValueError, "source_root_mismatch"):
            validation.source_identity(nested)

    def test_revision_mismatch_blocks_before_commands(self) -> None:
        check = self.create(revision="0" * 40)
        with self.assertRaises(validation.Halt):
            check.preflight(())
        self.assertEqual(self.finish(check), 1)
        self.assertEqual(check.report["reason"], "source_revision_mismatch")
        self.assertTrue(all(stage["status"] == "not_run" for stage in check.report["stages"]))

    def test_preflight_does_not_promote_unrun_stages(self) -> None:
        check = self.create(preflight=True)
        check.preflight(())
        self.assertEqual(self.finish(check), 0)
        self.assertEqual(check.report["status"], "preflight_passed")
        self.assertTrue(all(stage["status"] == "not_run" for stage in check.report["stages"]))
        self.assertNotIn(str(self.root), check.path.read_text(encoding="utf-8"))

    def test_missing_tool_is_reported_without_running(self) -> None:
        check = self.create()
        check.preflight(())
        with patch.object(validation.shutil, "which", return_value=None):
            with self.assertRaises(validation.Halt):
                check.preflight(("java",))
        self.assertEqual(check.report["tools"]["java"]["status"], "missing")
        self.assertEqual(self.finish(check), 1)

    def test_existing_lock_is_not_removed(self) -> None:
        first = self.create()
        first.acquire()
        other = self.create()
        with self.assertRaises(validation.Halt):
            other.acquire()
        self.finish(other)
        self.assertTrue(first.lock.is_dir())
        self.finish(first)
        self.assertFalse(first.lock.exists())

    def test_source_change_prevents_success(self) -> None:
        check = self.create(names=[])
        check.preflight(())
        (self.root / "source.txt").write_text("new\n", encoding="utf-8")
        self.assertEqual(self.finish(check), 1)
        self.assertEqual(check.report["reason"], "source_changed_during_validation")

    def test_failure_keeps_logs_and_unrun_stages(self) -> None:
        check = self.create()
        check.preflight(())
        check.acquire()
        with self.assertRaises(validation.Halt):
            check.run(
                "first",
                [
                    sys.executable,
                    "-S",
                    "-c",
                    "import sys; print('out'); print('err',file=sys.stderr); sys.exit(9)",
                ],
            )
        self.assertEqual(self.finish(check), 1)
        first, second = check.report["stages"]
        self.assertEqual(
            (first["status"], first["exit_code"], first["cleanup"]), ("failed", 9, "passed")
        )
        self.assertEqual(second["status"], "not_run")
        self.assertEqual((check.directory / first["stdout"]).read_text().strip(), "out")
        self.assertEqual((check.directory / first["stderr"]).read_text().strip(), "err")
        self.assertFalse(check.lock.exists())
        self.assertEqual(json.loads(check.path.read_text())["failed_stage"], "first")

    def test_success_and_evidence_directory_are_unique(self) -> None:
        check = self.create(names=["first"])
        check.preflight(())
        check.acquire()
        output = check.run("first", [sys.executable, "-S", "-c", "print('value')"])
        self.assertEqual(output.strip(), "value")
        self.assertEqual(self.finish(check), 0)
        other = self.create()
        self.assertNotEqual(check.directory, other.directory)
        self.finish(other)

    def test_output_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as external:
            try:
                (self.root / "reports").symlink_to(external, target_is_directory=True)
            except OSError:
                self.skipTest("Symbolic link creation is unavailable")
            with self.assertRaisesRegex(ValueError, "output_link_rejected"):
                self.create()
            self.assertEqual(list(Path(external).iterdir()), [])

    def test_expired_budget_does_not_start_command(self) -> None:
        check = self.create(names=["first"])
        check.preflight(())
        check.deadline = time.monotonic() - 1
        with self.assertRaises(validation.Halt):
            check.run("first", [sys.executable, "-S", "-c", "raise RuntimeError('not run')"])
        self.assertEqual(check.report["stages"][0]["cleanup"], "not_run")
        self.assertEqual(self.finish(check), 1)

    def test_source_missing_git_remains_blocked(self) -> None:
        with patch.object(validation, "git", side_effect=FileNotFoundError()):
            check = self.create()
            with self.assertRaises(validation.Halt):
                check.preflight(())
            self.assertEqual(self.finish(check), 1)
            self.assertIsNone(check.report["source"])


class ProcessExecutionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def execute(self, code: str, timeout: float = 10) -> dict:
        return processes.execute(
            [sys.executable, "-S", "-c", code], self.root, self.root, "command", timeout
        )

    def test_success_does_not_retain_raw_command(self) -> None:
        result = self.execute("print('ok')")
        self.assertEqual(
            (result["status"], result["exit_code"], result["cleanup"]), ("passed", 0, "passed")
        )
        self.assertFalse((self.root / "command.command.json").exists())

    def test_missing_executable_is_a_failure_with_diagnostic(self) -> None:
        result = processes.execute(
            [str(self.root / "absent-program")], self.root, self.root, "missing", 10
        )
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["error_type"], "FileNotFoundError")
        self.assertIn("FileNotFoundError", (self.root / "missing.stderr.log").read_text())

    def test_timeout_retires_root_and_descendant_without_killing_unrelated_process(self) -> None:
        independent = subprocess.Popen([sys.executable, "-S", "-c", "import time; time.sleep(60)"])
        try:
            code = (
                "import subprocess,sys,time; child=subprocess.Popen([sys.executable,'-S','-c',"
                '"import time; time.sleep(60)"]); print(child.pid,flush=True); time.sleep(60)'
            )
            result = self.execute(code, 2)
            self.assertEqual(result["status"], "timed_out")
            self.assertEqual(result["cleanup"], "passed")
            self.assertIsNone(independent.poll())
            self.assertTrue((self.root / "command.stdout.log").read_text().strip().isdigit())
        finally:
            independent.kill()
            independent.wait(timeout=5)

    def test_successful_root_with_leftover_child_is_not_success(self) -> None:
        code = (
            "import subprocess,sys; subprocess.Popen([sys.executable,'-S','-c',"
            "\"import time; time.sleep(60)\"]); print('root done')"
        )
        result = self.execute(code)
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["status"], "failed")
        self.assertTrue(result["descendants_terminated"])
        self.assertEqual(result["cleanup"], "passed")

    def test_repeated_execution_does_not_grow_descriptors(self) -> None:
        before = len(os.listdir("/proc/self/fd")) if sys.platform == "linux" else None
        for index in range(5):
            result = processes.execute(
                [sys.executable, "-S", "-c", "pass"], self.root, self.root, f"run-{index}", 10
            )
            self.assertEqual(result["status"], "passed")
        if before is not None:
            self.assertEqual(len(os.listdir("/proc/self/fd")), before)

    def test_stage_outputs_are_never_overwritten(self) -> None:
        self.execute("print('original')")
        with self.assertRaisesRegex(ValueError, "stage_output_exists"):
            self.execute("print('replacement')")
        self.assertEqual((self.root / "command.stdout.log").read_text().strip(), "original")

    def test_invalid_timeout_is_rejected(self) -> None:
        for timeout in (0, -1, float("inf"), float("nan")):
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                self.execute("pass", timeout)


class EvidenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / "guest/build/run/clientGameTest/screenshots"
        self.build.mkdir(parents=True)
        self.output = self.root / "reports/run"
        self.output.mkdir(parents=True)

    def image(self, name: str, prefix: str = "") -> Path:
        path = self.build / (prefix + name)
        path.write_bytes(
            b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 1, 1)
        )
        return path

    def test_old_screenshots_cannot_satisfy_new_run(self) -> None:
        for name in validation.SCREENSHOTS:
            self.image(name)
        before = validation.evidence_snapshot(self.root)
        result = validation.collect_evidence(self.root, self.output, before, screenshots=True)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(len(result["missing_screenshots"]), 3)
        self.assertEqual(result["files"], [])

    def test_new_counter_prefixed_screenshots_are_copied_and_hashed(self) -> None:
        before = validation.evidence_snapshot(self.root)
        for number, name in enumerate(sorted(validation.SCREENSHOTS)):
            self.image(name, f"{number:04d}_")
        result = validation.collect_evidence(self.root, self.output, before, screenshots=True)
        self.assertEqual(result["status"], "passed")
        self.assertEqual(len(result["files"]), 3)
        self.assertEqual(result["visual_review"], "not_run")
        for artifact in result["files"]:
            self.assertEqual(
                validation.digest(self.output / artifact["path"], 1024), artifact["sha256"]
            )

    def test_corrupt_screenshot_is_not_accepted(self) -> None:
        (self.build / sorted(validation.SCREENSHOTS)[0]).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "screenshot_header_invalid"):
            validation.collect_evidence(self.root, self.output, {}, screenshots=True)

    def test_world_files_are_not_collected(self) -> None:
        world = self.build.parent / "saves/world"
        world.mkdir(parents=True)
        (world / "world.xml").write_text("world", encoding="utf-8")
        self.assertEqual(validation.evidence_snapshot(self.root), {})

    def test_evidence_limits_fail_instead_of_silently_truncating(self) -> None:
        self.image(sorted(validation.SCREENSHOTS)[0])
        with patch.object(validation, "MAX_EVIDENCE_FILES", 0):
            with self.assertRaisesRegex(ValueError, "evidence_limit_exceeded"):
                validation.evidence_snapshot(self.root)

    def test_unreadable_evidence_directory_fails(self) -> None:
        base = self.root / "guest/build"
        base.mkdir(parents=True, exist_ok=True)

        def fail_walk(path, *, onerror):
            onerror(PermissionError("unreadable directory"))
            return iter(())

        with patch.object(validation.os, "walk", side_effect=fail_walk):
            with self.assertRaisesRegex(ValueError, "evidence_enumeration_failed"):
                validation.evidence_snapshot(self.root)


class EntryPointTests(SourceFixture):
    def test_options_preserve_gradle_arguments(self) -> None:
        settings, forwarded = validation.options(
            [
                "--hc-preflight",
                "build",
                "--hc-timeout",
                "45",
                "-PpeerExecutable=peer",
                "--hc-revision",
                "a" * 40,
            ],
            300,
        )
        self.assertEqual(settings, {"preflight": True, "timeout": 45, "revision": "a" * 40})
        self.assertEqual(forwarded, ["build", "-PpeerExecutable=peer"])

    def test_invalid_options_are_specific(self) -> None:
        for args in (
            ["--hc-unknown"],
            ["--hc-timeout"],
            ["--hc-timeout", "nan"],
            ["--hc-timeout", "0"],
            ["--hc-revision", "short"],
            ["--hc-preflight", "--hc-preflight"],
        ):
            with self.subTest(args=args), self.assertRaises(ValueError):
                validation.options(args, 120)

    def test_gradle_eula_refusal_precedes_bootstrap(self) -> None:
        check = self.create(names=["bootstrap", "gradle", "evidence"])
        with patch.object(gradle, "prepare") as prepare:
            with self.assertRaises(validation.Halt):
                gradle.validate(check, ["runClientGameTest"])
            prepare.assert_not_called()
        self.assertEqual(check.report["reason"], "minecraft_opt_in_required")
        self.finish(check)

    def test_bridge_preflight_does_not_build_or_run(self) -> None:
        check = self.create(names=test_bridge.STAGES, preflight=True)
        with patch.object(check, "preflight") as preflight, patch.object(check, "run") as run:
            test_bridge.validate(check, [])
            preflight.assert_called_once_with(("cmake", "ctest", "java", "javac"))
            run.assert_not_called()
        self.finish(check)

    def test_gradle_preflight_never_downloads(self) -> None:
        (self.root / "guest").mkdir()
        (self.root / "guest/gradle.properties").write_text(
            "minecraft_version=1.21.4\n", encoding="utf-8"
        )
        check = self.create(names=["bootstrap", "gradle", "evidence"], preflight=True)
        with (
            patch.object(gradle, "ROOT", self.root),
            patch.object(check, "preflight"),
            patch.object(check, "run") as run,
            patch.object(gradle, "prepare") as prepare,
        ):
            gradle.validate(check, ["build"])
            run.assert_not_called()
            prepare.assert_not_called()
        self.assertEqual(check.report["gradle_cache"], "missing")
        self.finish(check)

    def test_unexpected_exception_keeps_failure_log(self) -> None:
        def broken(check, arguments):
            raise RuntimeError("controlled failure")

        with contextlib.redirect_stdout(io.StringIO()):
            result = validation.entry(self.root, "controlled", [], 30, ["first"], broken)
        self.assertEqual(result, 1)
        path = next((self.root / "reports/validation").glob("*/result.json"))
        data = json.loads(path.read_text())
        self.assertEqual(data["status"], "failed")
        self.assertIn("controlled failure", (path.parent / data["diagnostic"]).read_text())


class AdditionalProcessTests(unittest.TestCase):
    setUp = ProcessExecutionTests.setUp
    execute = ProcessExecutionTests.execute

    def test_interruption_reports_cleanup(self) -> None:
        sleep = time.sleep
        calls = 0

        def interrupted_once(seconds):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise KeyboardInterrupt
            sleep(seconds)

        with patch.object(processes.time, "sleep", side_effect=interrupted_once):
            result = self.execute("import time; time.sleep(60)")
        self.assertEqual(result["status"], "interrupted")
        self.assertEqual(result["cleanup"], "passed")

    @unittest.skipUnless(sys.platform == "linux", "Linux signal handling")
    def test_ignored_termination_signal_is_escalated(self) -> None:
        result = self.execute(
            "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)", 2
        )
        self.assertEqual(result["status"], "timed_out")
        self.assertEqual(result["cleanup"], "passed")

    def test_fast_oversized_log_does_not_pass(self) -> None:
        with patch.object(processes, "LOG_LIMIT", 1024):
            result = self.execute("print('x' * 16384)")
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["reason"], "log_limit_exceeded")
        self.assertEqual(result["cleanup"], "passed")

    @unittest.skipUnless(os.name == "nt", "Windows batch launcher")
    def test_windows_batch_launcher_in_directory_with_spaces(self) -> None:
        directory = self.root / "tool chain"
        directory.mkdir()
        launcher = directory / "run.bat"
        launcher.write_text("@echo off\r\necho launched\r\nexit /b 0\r\n", encoding="ascii")
        result = processes.execute([str(launcher)], self.root, self.root, "batch", 10)
        self.assertEqual(result["status"], "passed")
        self.assertEqual((self.root / "batch.stdout.log").read_text().strip(), "launched")

    @unittest.skipUnless(os.name == "nt", "Windows Job Object ownership")
    def test_windows_job_reports_active_process_names(self) -> None:
        job = processes.WindowsJob()
        process = subprocess.Popen([sys.executable, "-S", "-c", "import time; time.sleep(10)"])
        try:
            job.attach(process.pid)
            names = job.active_process_names()
            self.assertIn("python.exe", [name.lower() for name in names])
        finally:
            try:
                if process.poll() is None:
                    job.terminate()
                process.wait(timeout=5)
            finally:
                job.close()

    @unittest.skipUnless(os.name == "nt", "Windows Job Object ownership")
    def test_windows_assignment_failure_never_releases_command(self) -> None:
        marker = self.root / "unexpected.txt"
        with patch.object(
            processes.WindowsJob, "attach", side_effect=OSError("assignment refused")
        ):
            result = self.execute(
                f"from pathlib import Path; Path({str(marker)!r}).write_text('ran')"
            )
        self.assertEqual(result["status"], "failed")
        self.assertFalse(marker.exists())


class GradleStageTests(SourceFixture):
    def setUp(self) -> None:
        super().setUp()
        (self.root / "guest").mkdir()
        (self.root / "guest/gradle.properties").write_text(
            "minecraft_version=1.21.4\n", encoding="utf-8"
        )

    def launch(self, bootstrap_fails: bool) -> validation.Validation:
        check = self.create(names=["bootstrap", "gradle", "evidence"])
        real_run = check.run

        def stage(name, command, limit=120):
            if name == "bootstrap":
                code = (
                    "raise SystemExit(4)"
                    if bootstrap_fails
                    else 'print(\'{"executable":"controlled"}\')'
                )
            else:
                code = (
                    "from pathlib import Path; p=Path('guest/build/test-results/result.xml'); "
                    "p.parent.mkdir(parents=True,exist_ok=True); p.write_text('<failure/>'); "
                    "print('> Task :compileJava FAILED'); raise SystemExit(3)"
                )
            return real_run(name, [sys.executable, "-S", "-c", code], limit)

        real_preflight = check.preflight
        with (
            patch.object(gradle, "ROOT", self.root),
            patch.object(check, "run", side_effect=stage),
            patch.object(check, "preflight", side_effect=lambda tools: real_preflight(())),
        ):
            with self.assertRaises(validation.Halt):
                gradle.validate(check, ["build"])
        self.assertEqual(self.finish(check), 1)
        self.assertFalse(check.lock.exists())
        return check

    def test_bootstrap_failure_does_not_run_gradle(self) -> None:
        check = self.launch(True)
        self.assertEqual(check.report["failed_stage"], "bootstrap")
        self.assertEqual(check.report["stages"][1]["status"], "not_run")
        self.assertEqual(check.report["stages"][0]["exit_code"], 4)

    def test_failed_gradle_task_preserves_changed_evidence(self) -> None:
        check = self.launch(False)
        self.assertEqual(check.report["failed_stage"], "gradle")
        self.assertEqual(check.report["stages"][1]["exit_code"], 3)
        self.assertEqual(check.report["gradle_failed_tasks"], [":compileJava"])
        self.assertEqual(len(check.report["stages"][2]["files"]), 1)
        self.assertEqual(check.report["status"], "failed")

    def test_dry_run_cannot_be_reported_as_execution(self) -> None:
        check = self.create(names=["bootstrap", "gradle", "evidence"])
        with self.assertRaises(validation.Halt):
            gradle.validate(check, ["build", "--dry-run"])
        self.assertEqual(check.report["reason"], "validation_dry_run_rejected")
        self.finish(check)

    def test_only_exact_validation_tasks_without_exclusions_are_accepted(self) -> None:
        for arguments in (["help"], ["clean"], ["runC"], ["runClient"], ["build", "-x", "test"]):
            with self.subTest(arguments=arguments):
                check = self.create(names=["bootstrap", "gradle", "evidence"])
                with self.assertRaises(validation.Halt):
                    gradle.validate(check, arguments)
                self.assertTrue(
                    all(stage["status"] == "not_run" for stage in check.report["stages"])
                )
                self.assertEqual(self.finish(check), 1)

    def test_both_requested_game_tasks_must_execute(self) -> None:
        peer = self.root / "build/peer"
        peer.parent.mkdir()
        peer.write_text("controlled peer\n", encoding="utf-8")
        peer.chmod(0o755)
        check = self.create(names=["bootstrap", "gradle", "evidence"])
        real_run = check.run
        real_preflight = check.preflight

        def stage(name, command, limit=120):
            code = (
                'print(\'{"executable":"controlled"}\')'
                if name == "bootstrap"
                else "print('> Task :runGameTest SKIPPED'); print('> Task :runClientGameTest')"
            )
            return real_run(name, [sys.executable, "-S", "-c", code], limit)

        with (
            patch.object(gradle, "ROOT", self.root),
            patch.object(check, "run", side_effect=stage),
            patch.object(check, "preflight", side_effect=lambda tools: real_preflight(())),
            patch.dict(os.environ, {"DISPLAY": ":controlled"}),
            patch.object(
                gradle, "collect_evidence", return_value={"status": "passed", "files": []}
            ),
        ):
            gradle.validate(
                check,
                [
                    "runGameTest",
                    "runClientGameTest",
                    "-PrunMinecraftTests=true",
                    "-PacceptMinecraftEula=true",
                    "-PpeerExecutable=../build/peer",
                ],
            )
        self.assertEqual(self.finish(check), 1)
        self.assertEqual(check.report["unexecuted_game_tasks"], ["runGameTest"])
        self.assertEqual(check.report["reason"], "game_task_not_executed")


class ReportSchemaTests(SourceFixture):
    def test_generated_reports_validate_and_invalid_status_is_rejected(self) -> None:
        import jsonschema

        schema = json.loads(
            (Path(__file__).resolve().parents[1] / "schemas/validation-v1.schema.json").read_text()
        )
        validator = jsonschema.Draft202012Validator(
            schema, format_checker=jsonschema.FormatChecker()
        )
        jsonschema.Draft202012Validator.check_schema(schema)
        check = self.create(preflight=True)
        validator.validate(check.report)
        check.preflight(())
        self.finish(check)
        validator.validate(check.report)
        check.report["stages"][0]["status"] = "untested_pass"
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(check.report)
