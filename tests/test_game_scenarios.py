import json
import unittest
from pathlib import Path

import jsonschema
from tools.game_scenarios import CLIENT_SCENARIOS, LAN_SCENARIOS, client_results, lan_requested


class ClientScenarioTests(unittest.TestCase):
    def output(self, names) -> str:
        return "\n".join(f"HARRANCRAFT_SCENARIO {name}" for name in sorted(names))

    def test_default_client_requires_all_world_scenarios(self) -> None:
        self.assertEqual(
            CLIENT_SCENARIOS,
            {
                "block-roundtrip",
                "reconnect",
                "block-entity-preservation",
                "thread-ownership",
                "world-stop",
                "world-reopen",
                "world-replacement",
            },
        )
        result = client_results(self.output(CLIENT_SCENARIOS), lan=False)
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["lan"], "not_requested")

    def test_each_missing_scenario_fails(self) -> None:
        for scenario in CLIENT_SCENARIOS | LAN_SCENARIOS:
            with self.subTest(scenario=scenario):
                result = client_results(
                    self.output((CLIENT_SCENARIOS | LAN_SCENARIOS) - {scenario}), lan=True
                )
                self.assertEqual(result["status"], "failed")
                self.assertEqual(result["missing"], [scenario])

    def test_lan_requires_both_transition_and_recovery(self) -> None:
        self.assertEqual(LAN_SCENARIOS, {"lan-revocation", "lan-recovery"})
        self.assertEqual(
            client_results(self.output(CLIENT_SCENARIOS | LAN_SCENARIOS), lan=True)["status"],
            "passed",
        )
        result = client_results(self.output(CLIENT_SCENARIOS), lan=True)
        self.assertEqual(result["missing"], sorted(LAN_SCENARIOS))

    def test_duplicates_and_unrequested_lan_do_not_pass(self) -> None:
        output = self.output(CLIENT_SCENARIOS) + "\nHARRANCRAFT_SCENARIO reconnect"
        result = client_results(output, lan=False)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["duplicates"], ["reconnect"])
        result = client_results(self.output(CLIENT_SCENARIOS | LAN_SCENARIOS), lan=False)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["unexpected"], sorted(LAN_SCENARIOS))

    def test_gradle_exit_and_native_stages_are_not_game_evidence(self) -> None:
        for output in (
            "BUILD SUCCESSFUL\n> Task :runClientGameTest",
            '{"stage":"passed","epoch":7}',
            "HARRANCRAFT_SCENARIO world-stop failed",
            "HARRANCRAFT_SCENARIO world-stop:passed",
            "HARRANCRAFT_SCENARIO world-stop-more",
        ):
            with self.subTest(output=output):
                self.assertEqual(client_results(output, lan=False)["status"], "failed")

    def test_log_prefix_and_windows_newlines_preserve_results(self) -> None:
        output = "\r\n".join(
            f"[10:00:00] [Client GameTest/INFO]: HARRANCRAFT_SCENARIO {name}"
            for name in CLIENT_SCENARIOS
        )
        self.assertEqual(client_results(output, lan=False)["status"], "passed")

    def test_invalid_marker_cannot_pass_with_other_required_scenarios(self) -> None:
        valid = self.output(CLIENT_SCENARIOS)
        for malformed in (
            "bad_name",
            "reconnect passed",
            "",
            "UPPERCASE",
            "bad.name",
            "reconnect\tfailed",
        ):
            with self.subTest(malformed=malformed):
                result = client_results(valid + "\nHARRANCRAFT_SCENARIO " + malformed, lan=False)
                self.assertEqual(result["status"], "failed")
                self.assertEqual(result["invalid_records"], 1)

    def test_scenario_record_overflow_is_a_failure(self) -> None:
        with self.assertRaisesRegex(ValueError, "client_scenario_limit_exceeded"):
            client_results("HARRANCRAFT_SCENARIO reconnect\n" * 65, lan=False)

    def test_lan_requires_explicit_client_selection(self) -> None:
        for enabled in (False, True):
            for client in (False, True):
                with self.subTest(enabled=enabled, client=client):
                    self.assertFalse(lan_requested({}, enabled=enabled, client=client))
                    if enabled and client:
                        self.assertTrue(
                            lan_requested({"runLanTests": "true"}, enabled=enabled, client=client)
                        )
                    else:
                        with self.assertRaisesRegex(ValueError, "lan_client_opt_in_required"):
                            lan_requested({"runLanTests": "true"}, enabled=enabled, client=client)

    def test_lan_false_and_invalid_values(self) -> None:
        self.assertFalse(lan_requested({"runLanTests": "false"}, enabled=True, client=True))
        for value in ("", "TRUE", "1", "yes", "false\n"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                lan_requested({"runLanTests": value}, enabled=True, client=True)

    def test_result_contract_validates_scenarios_and_rejects_invalid_lan_type(self) -> None:
        schema_path = Path(__file__).resolve().parents[1] / "schemas/validation-v1.schema.json"
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        report = {
            "schema_version": 1,
            "component": "fabric",
            "status": "passed",
            "started_at": "2026-10-16T00:00:00Z",
            "platform": {"system": "controlled", "machine": "controlled", "python": "3.13"},
            "source": None,
            "tools": {},
            "preflight": "passed",
            "stages": [],
            "lan_execution": False,
            "client_scenarios": client_results(self.output(CLIENT_SCENARIOS), lan=False),
        }
        validator = jsonschema.Draft202012Validator(schema)
        validator.validate(report)
        report["lan_execution"] = "false"
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(report)
