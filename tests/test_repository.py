import json
import tempfile
import unittest
from pathlib import Path

from tools.check_repository import (
    anchors,
    check_markdown,
    check_workflow,
    read_source,
    source_files,
    unique_object,
)


class RepositoryContractTests(unittest.TestCase):
    def test_duplicate_json_keys_fail(self) -> None:
        with self.assertRaises(ValueError):
            json.loads('{"key":1,"key":2}', object_pairs_hook=unique_object)

    def test_duplicate_and_unicode_heading_anchors(self) -> None:
        self.assertEqual(anchors("# Test\n# Test\n## 中文 标题\n"), {"test", "test-1", "中文-标题"})

    def test_broken_reference_and_anchor_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "a.md"
            (root / "b.md").write_text("# Target\n", encoding="utf-8")
            path.write_text("[link](b.md#target)\n", encoding="utf-8")
            self.assertEqual(check_markdown(path, root), 1)
            for value in (
                "[link](b.md#missing)\n",
                "[link][absent]\n",
                "[link](missing.md)\n",
                "[link](../outside.md)\n",
            ):
                with self.subTest(value=value):
                    path.write_text(value, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        check_markdown(path, root)

    def test_fenced_examples_are_not_document_links(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "a.md"
            path.write_text("```text\n[link](missing.md)\n```\n", encoding="utf-8")
            self.assertEqual(check_markdown(path, root), 0)

    def test_ci_refuses_write_permissions_and_unpinned_actions(self) -> None:
        baseline = {
            "on": {"pull_request": None, "workflow_dispatch": None},
            "permissions": {"contents": "read"},
            "jobs": {
                "checks": {
                    "steps": [
                        {
                            "uses": "actions/checkout@" + "a" * 40,
                            "with": {"persist-credentials": "false"},
                        }
                    ]
                }
            },
        }
        check_workflow(baseline)
        for mutation in ("permissions", "action", "credentials", "trigger"):
            with self.subTest(mutation=mutation):
                candidate = json.loads(json.dumps(baseline))
                if mutation == "permissions":
                    candidate["permissions"]["contents"] = "write"
                elif mutation == "action":
                    candidate["jobs"]["checks"]["steps"][0]["uses"] = "actions/checkout@main"
                elif mutation == "credentials":
                    candidate["jobs"]["checks"]["steps"][0]["with"]["persist-credentials"] = "true"
                else:
                    candidate["on"]["schedule"] = []
                with self.assertRaises(ValueError):
                    check_workflow(candidate)

    def test_bridge_source_text_is_checked(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for suffix in (".java", ".cpp", ".hpp", ".gradle", ".properties", ".cmake"):
                path = root / ("source" + suffix)
                with self.subTest(suffix=suffix):
                    path.write_bytes(b"valid\n")
                    self.assertEqual(read_source(path), "valid\n")
                    for invalid in (
                        b"bom\r\n",
                        b"missing newline",
                        b"space \n",
                        b"\xef\xbb\xbftext\n",
                    ):
                        path.write_bytes(invalid)
                        with self.assertRaises(ValueError):
                            read_source(path)

    def test_java_and_native_outputs_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for suffix in (".class", ".jar", ".exe", ".o", ".so", ".dylib"):
                path = Path(temporary) / ("output" + suffix)
                path.write_bytes(b"output")
                with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                    read_source(path)

    def test_game_and_toolchain_caches_are_pruned(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            excluded = (
                "build/toolchains/gradle/bin/gradle.jar",
                "guest/.gradle/cache/entry.class",
                "guest/build/classes/Main.class",
                "guest/run/level.dat",
                "guest/runClientGameTest/level.dat",
                "guest/runGameTest/level.dat",
            )
            included = ("guest/build.gradle", "guest/src/Main.java", "native/run/source.cpp")
            for name in (*excluded, *included):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("source\n", encoding="utf-8")
            self.assertEqual(
                {path.relative_to(root).as_posix() for path in source_files(root)}, set(included)
            )

    def test_additional_actions_and_job_permissions_remain_rejected(self) -> None:
        for action in ("setup-java", "upload-artifact", "unreviewed"):
            data = {
                "on": {"pull_request": None, "workflow_dispatch": None},
                "permissions": {"contents": "read"},
                "jobs": {"checks": {"steps": [{"uses": f"actions/{action}@" + "a" * 40}]}},
            }
            with self.subTest(action=action), self.assertRaises(ValueError):
                check_workflow(data)
        data["jobs"]["checks"] = {"permissions": {"contents": "write"}, "steps": []}
        with self.assertRaises(ValueError):
            check_workflow(data)
