from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from fixtures import image_bytes
from harrancraft_tools.i18n import catalog, select_language, text

ROOT = Path(__file__).resolve().parents[1]


class CLITests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "private-probe.dll"
        self.path.write_bytes(image_bytes())

    def command(self, *args: str) -> subprocess.CompletedProcess[str]:
        env = dict(os.environ, PYTHONPATH=str(ROOT / "src"), PYTHONIOENCODING="utf-8", LANG="en_US.UTF-8")
        return subprocess.run([sys.executable, "-m", "harrancraft_tools", *args],
                              capture_output=True, text=True, encoding="utf-8", env=env, timeout=15)

    def test_process_json_report_and_no_paths(self) -> None:
        result = self.command("--json", "--exports", str(self.path))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(report["results"][0]["status"], "inspected")
        self.assertNotIn(self.temp.name, result.stdout)
        self.assertNotIn(self.path.name, result.stdout)
        self.assertEqual(self.path.read_bytes(), image_bytes())

    def test_mixed_results_have_nonzero_exit_and_keep_input_order(self) -> None:
        result = self.command("--json", str(self.path), str(self.path.parent / "missing.dll"))
        self.assertEqual(result.returncode, 1)
        entries = json.loads(result.stdout)["results"]
        self.assertEqual([e["input_index"] for e in entries], [1, 2])
        self.assertEqual([e["status"] for e in entries], ["inspected", "error"])
        self.assertNotIn(self.temp.name, result.stdout + result.stderr)

    def test_chinese_success_and_error(self) -> None:
        result = self.command("--language", "zh-CN", str(self.path))
        self.assertEqual(result.returncode, 0)
        self.assertIn("输入 1", result.stdout)
        self.assertIn("文件版本", result.stdout)
        bad = self.command("--json", "--language", "zh-CN", str(self.path.parent / "missing.dll"))
        error = json.loads(bad.stdout)["results"][0]["error"]
        self.assertEqual(error["code"], "input_unreadable")
        self.assertIn("无法读取", error["message"])

    def test_help_version_and_argument_failures(self) -> None:
        for args in (("--help",), ("--version",), ("--help", "--language", "zh-CN")):
            with self.subTest(args=args):
                result = self.command(*args)
                self.assertEqual(result.returncode, 0)
                self.assertTrue(result.stdout.strip())
        for args in ((), ("--unknown-private-path",), ("--language",), ("--jso",), tuple([str(self.path)] * 17)):
            with self.subTest(args=args):
                result = self.command(*args)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertNotIn(self.temp.name, result.stderr)
                self.assertNotIn("unknown-private-path", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_double_dash_filename(self) -> None:
        result = self.command("--json", "--", str(self.path))
        self.assertEqual(result.returncode, 0)

    def test_schema_validates_success_failure_and_exports(self) -> None:
        import jsonschema
        schema = json.loads((ROOT / "schemas/inspection-v1.schema.json").read_text())
        jsonschema.Draft202012Validator.check_schema(schema)
        for flags in ([], ["--exports"]):
            result = self.command("--json", *flags, str(self.path), str(self.path.parent / "missing.dll"))
            jsonschema.validate(json.loads(result.stdout), schema)


class LanguageTests(unittest.TestCase):
    def test_language_normalization_and_fallback(self) -> None:
        for value, expected in (("zh_CN.UTF-8", "zh-CN"), ("zh-Hans", "zh-CN"),
                                ("en-GB", "en-US"), ("unknown", "en-US"), ("zh-TW", "en-US")):
            with self.subTest(value=value):
                self.assertEqual(select_language(value), expected)
        with patch.dict(os.environ, {"LC_ALL": "zh_CN.UTF-8", "LANG": "en_US.UTF-8"}, clear=True):
            self.assertEqual(select_language(), "zh-CN")

    def test_catalog_keys_and_placeholders(self) -> None:
        from string import Formatter
        english, chinese = catalog("en-US"), catalog("zh-CN")
        self.assertEqual(english.keys(), chinese.keys())
        formatter = Formatter()
        for key in english:
            def fields(value):
                return sorted(name for _, name, _, _ in formatter.parse(value) if name is not None)
            with self.subTest(key=key):
                self.assertEqual(fields(english[key]), fields(chinese[key]))
                self.assertTrue(english[key].strip() and chinese[key].strip())

    def test_missing_translation_uses_english(self) -> None:
        original = catalog
        def missing(language):
            return {} if language == "zh-CN" else original(language)
        with patch("harrancraft_tools.i18n.catalog", side_effect=missing):
            self.assertEqual(text("error.invalid_pe", "zh-CN"), original("en-US")["error.invalid_pe"])
