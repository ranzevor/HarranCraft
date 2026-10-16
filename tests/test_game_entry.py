import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import gradle, validation
from tools.game_scenarios import CLIENT_SCENARIOS, LAN_SCENARIOS


class GameEntryTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / "guest").mkdir()
        (self.root / "guest/gradle.properties").write_text(
            "minecraft_version=1.21.4\n", encoding="utf-8"
        )
        peer = self.root / "peer"
        peer.write_text("controlled\n", encoding="utf-8")
        peer.chmod(0o700)
        self.check = validation.Validation(
            self.root,
            "controlled",
            {"timeout": 30, "revision": None, "preflight": False},
            ["bootstrap", "gradle", "evidence"],
        )
        self.commands = {}

    def launch(self, markers, *, lan=False) -> dict:
        def stage(name, command, limit):
            self.commands[name] = command
            entry = next(item for item in self.check.report["stages"] if item["name"] == name)
            entry["status"] = "passed"
            if name == "bootstrap":
                return json.dumps({"executable": "controlled"})
            output = "> Task :runClientGameTest\n" + "\n".join(
                "HARRANCRAFT_SCENARIO " + marker for marker in markers
            )
            (self.check.directory / "gradle.stdout.log").write_text(output, encoding="utf-8")
            return output

        arguments = [
            "runClientGameTest",
            "-PrunMinecraftTests=true",
            "-PacceptMinecraftEula=true",
            "-PpeerExecutable=../peer",
        ]
        if lan:
            arguments.append("-PrunLanTests=true")
        with (
            patch.object(gradle, "ROOT", self.root),
            patch.object(self.check, "preflight"),
            patch.object(self.check, "acquire"),
            patch.object(self.check, "run", side_effect=stage),
            patch.object(gradle, "collect_evidence", return_value={"status": "passed"}),
            patch.dict(
                os.environ, {"DISPLAY": ":controlled", "ORG_GRADLE_PROJECT_runLanTests": "true"}
            ),
        ):
            gradle.validate(self.check, arguments)
        return self.check.report

    def test_default_explicitly_disables_ambient_lan(self) -> None:
        result = self.launch(CLIENT_SCENARIOS)
        self.assertFalse(result["lan_execution"])
        self.assertEqual(result["client_scenarios"]["status"], "passed")
        self.assertEqual(self.commands["gradle"].count("-PrunLanTests=false"), 1)

    def test_game_task_and_screenshots_alone_are_incomplete(self) -> None:
        result = self.launch([])
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["reason"], "client_scenario_incomplete")
        self.assertEqual(result["client_scenarios"]["missing"], sorted(CLIENT_SCENARIOS))

    def test_explicit_lan_requires_recovery_marker(self) -> None:
        result = self.launch(CLIENT_SCENARIOS | {"lan-revocation"}, lan=True)
        self.assertTrue(result["lan_execution"])
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["client_scenarios"]["missing"], ["lan-recovery"])
        self.assertEqual(self.commands["gradle"].count("-PrunLanTests=true"), 1)

    def test_complete_explicit_lan_results_are_retained(self) -> None:
        result = self.launch(CLIENT_SCENARIOS | LAN_SCENARIOS, lan=True)
        self.assertEqual(result["client_scenarios"]["status"], "passed")
        self.assertEqual(
            result["client_scenarios"]["passed"], sorted(CLIENT_SCENARIOS | LAN_SCENARIOS)
        )

    def test_build_opt_in_requires_server_gametest_not_client(self) -> None:
        def stage(name, command, limit):
            self.commands[name] = command
            entry = next(item for item in self.check.report["stages"] if item["name"] == name)
            entry["status"] = "passed"
            if name == "bootstrap":
                return json.dumps({"executable": "controlled"})
            output = "> Task :runGameTest\n> Task :build\n"
            (self.check.directory / "gradle.stdout.log").write_text(output, encoding="utf-8")
            return output

        with (
            patch.object(gradle, "ROOT", self.root),
            patch.object(self.check, "preflight"),
            patch.object(self.check, "acquire"),
            patch.object(self.check, "run", side_effect=stage),
            patch.object(gradle, "collect_evidence", return_value={"status": "passed"}),
            patch.dict(os.environ, {"DISPLAY": ""}),
        ):
            gradle.validate(
                self.check,
                ["build", "-PrunMinecraftTests=true", "-PacceptMinecraftEula=true"],
            )
        self.assertFalse(self.check.report["lan_execution"])
        self.assertEqual(self.check.report["unexecuted_game_tasks"], [])
        self.assertEqual(self.check.report["uncompleted_tasks"], [])
        self.assertNotIn("client_scenarios", self.check.report)
        self.assertIn("-PrunLanTests=false", self.commands["gradle"])

    def test_lan_without_client_is_rejected_before_preflight(self) -> None:
        for arguments in (
            ["build", "-PrunLanTests=true"],
            [
                "build",
                "-PrunMinecraftTests=true",
                "-PacceptMinecraftEula=true",
                "-PrunLanTests=true",
            ],
            [
                "runGameTest",
                "-PrunMinecraftTests=true",
                "-PacceptMinecraftEula=true",
                "-PrunLanTests=true",
            ],
        ):
            with (
                self.subTest(arguments=arguments),
                patch.object(self.check, "preflight") as preflight,
            ):
                with self.assertRaises(validation.Halt):
                    gradle.validate(self.check, arguments)
                preflight.assert_not_called()
                self.assertEqual(self.check.report["reason"], "lan_client_opt_in_required")
