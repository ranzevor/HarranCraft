import hashlib
import io
import json
import os
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from tools import gradle


class GradleBootstrapTests(unittest.TestCase):
    def archive(self, traversal: bool = False) -> bytes:
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            name = "gradle.bat" if os.name == "nt" else "gradle"
            archive.writestr(f"gradle-{gradle.VERSION}/bin/{name}", "unused test entry\n")
            if traversal:
                archive.writestr("../outside.txt", "untrusted")
        return data.getvalue()

    def test_verified_archive_and_cached_launcher(self) -> None:
        data = self.archive()
        with tempfile.TemporaryDirectory() as temporary:
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(gradle, "SHA256", hashlib.sha256(data).hexdigest()),
                patch.object(
                    gradle.urllib.request, "urlopen", return_value=io.BytesIO(data)
                ) as get,
            ):
                executable = gradle.prepare()
                self.assertTrue(executable.is_file())
                self.assertEqual(gradle.prepare(), executable)
                self.assertEqual(get.call_count, 1)

    def test_checksum_failure_does_not_install(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(
                    gradle.urllib.request, "urlopen", return_value=io.BytesIO(self.archive())
                ),
            ):
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    gradle.prepare()
                home = Path(temporary) / "build/toolchains" / f"gradle-{gradle.VERSION}"
                self.assertFalse(home.exists())

    def test_archive_cannot_escape_toolchain_directory(self) -> None:
        data = self.archive(traversal=True)
        with tempfile.TemporaryDirectory() as temporary:
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(gradle, "SHA256", hashlib.sha256(data).hexdigest()),
                patch.object(gradle.urllib.request, "urlopen", return_value=io.BytesIO(data)),
            ):
                with self.assertRaisesRegex(ValueError, "archive path"):
                    gradle.prepare()
                self.assertFalse((Path(temporary) / "build/toolchains/outside.txt").exists())

    def test_missing_launcher_is_not_promoted(self) -> None:
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr(f"gradle-{gradle.VERSION}/LICENSE", "test\n")
        payload = data.getvalue()
        with tempfile.TemporaryDirectory() as temporary:
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(gradle, "SHA256", hashlib.sha256(payload).hexdigest()),
                patch.object(gradle.urllib.request, "urlopen", return_value=io.BytesIO(payload)),
            ):
                with self.assertRaisesRegex(ValueError, "launcher"):
                    gradle.prepare()
                home = Path(temporary) / "build/toolchains" / f"gradle-{gradle.VERSION}"
                self.assertFalse(home.exists())
                self.assertFalse((Path(temporary) / "build/toolchains/.install-lock").exists())

    def test_invalid_cache_is_preserved_without_download(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary) / "build/toolchains" / f"gradle-{gradle.VERSION}"
            home.mkdir(parents=True)
            retained = home / "retained.txt"
            retained.write_text("existing cache\n", encoding="utf-8")
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(gradle.urllib.request, "urlopen") as get,
            ):
                with self.assertRaisesRegex(
                    ValueError, f"remove build/toolchains/gradle-{gradle.VERSION}"
                ):
                    gradle.prepare()
                get.assert_not_called()
                self.assertEqual(retained.read_text(encoding="utf-8"), "existing cache\n")

    def test_existing_install_lock_is_not_removed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary) / "build/toolchains/.install-lock"
            lock.mkdir(parents=True)
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(gradle.urllib.request, "urlopen") as get,
            ):
                with self.assertRaisesRegex(ValueError, "lock exists"):
                    gradle.prepare()
                get.assert_not_called()
                self.assertTrue(lock.is_dir())

    def test_extraction_rejects_aliases_and_special_files(self) -> None:
        for name, mode in (
            (f"gradle-{gradle.VERSION}/./entry", 0),
            (f"gradle-{gradle.VERSION}/../entry", 0),
            (f"gradle-{gradle.VERSION}/bin\\entry", 0),
            (f"gradle-{gradle.VERSION}/link", 0o120777),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                path = root / "source.zip"
                with zipfile.ZipFile(path, "w") as archive:
                    info = zipfile.ZipInfo(name)
                    info.filename = name
                    info.create_system = 3
                    info.external_attr = mode << 16
                    archive.writestr(info, "entry")
                with zipfile.ZipFile(path) as archive:
                    self.assertEqual(archive.infolist()[0].orig_filename, name)
                with self.assertRaises(ValueError):
                    gradle.unpack(path, root, f"gradle-{gradle.VERSION}")
                self.assertFalse((root / f"gradle-{gradle.VERSION}").exists())

    def test_extraction_rejects_normalized_archive_names(self) -> None:
        home = f"gradle-{gradle.VERSION}"
        launcher = "gradle.bat" if os.name == "nt" else "gradle"
        for name in (f"{home}/bin\\entry", f"{home}/entry\x00suffix"):
            for separator in ("/", "\\"):
                with (
                    self.subTest(name=name, separator=separator),
                    tempfile.TemporaryDirectory() as temporary,
                ):
                    root = Path(temporary)
                    path = root / "source.zip"
                    with zipfile.ZipFile(path, "w") as archive:
                        archive.writestr(f"{home}/bin/{launcher}", "test launcher\n")
                        info = zipfile.ZipInfo(name)
                        # Keep the raw name in both ZIP headers on every platform.
                        info.filename = name
                        archive.writestr(info, "entry")
                    with patch.object(zipfile.os, "sep", separator):
                        with zipfile.ZipFile(path) as archive:
                            self.assertEqual(archive.infolist()[1].orig_filename, name)
                        with self.assertRaisesRegex(ValueError, "archive path"):
                            gradle.unpack(path, root, home)
                    self.assertFalse((root / home).exists())

    def test_extraction_rejects_case_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "source.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr(f"gradle-{gradle.VERSION}/bin/gradle", "one")
                archive.writestr(f"gradle-{gradle.VERSION}/bin/GRADLE", "two")
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                gradle.unpack(path, root, f"gradle-{gradle.VERSION}")

    def test_download_and_extraction_are_bounded(self) -> None:
        for limit in ("MAX_DOWNLOAD", "MAX_ENTRIES", "MAX_EXPANDED"):
            payload = self.archive()
            with self.subTest(limit=limit), tempfile.TemporaryDirectory() as temporary:
                with (
                    patch.object(gradle, "ROOT", Path(temporary)),
                    patch.object(gradle, "SHA256", hashlib.sha256(payload).hexdigest()),
                    patch.object(gradle, limit, 0),
                    patch.object(
                        gradle.urllib.request, "urlopen", return_value=io.BytesIO(payload)
                    ),
                ):
                    with self.assertRaisesRegex(ValueError, "limit"):
                        gradle.prepare()
                    home = Path(temporary) / "build/toolchains" / f"gradle-{gradle.VERSION}"
                    self.assertFalse(home.exists())
                    self.assertFalse((Path(temporary) / "build/toolchains/.install-lock").exists())

    def test_network_failure_releases_install_lock(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with (
                patch.object(gradle, "ROOT", Path(temporary)),
                patch.object(
                    gradle.urllib.request, "urlopen", side_effect=OSError("network failure")
                ),
            ):
                with self.assertRaisesRegex(OSError, "network failure"):
                    gradle.prepare()
                self.assertFalse((Path(temporary) / "build/toolchains/.install-lock").exists())


class FabricManifestTests(unittest.TestCase):
    def test_game_tests_remain_in_separate_source_manifest(self) -> None:
        root = Path(__file__).resolve().parents[1]
        production_path = root / "guest/src/main/resources/fabric.mod.json"
        test_path = root / "guest/src/gametest/resources/fabric.mod.json"
        production = json.loads(production_path.read_text(encoding="utf-8"))
        test_mod = json.loads(test_path.read_text(encoding="utf-8"))

        test_keys = {"fabric-gametest", "fabric-client-gametest"}
        self.assertEqual(production["id"], "harrancraft")
        self.assertEqual(test_mod["id"], "harrancraft-gametest")
        self.assertTrue(test_keys.isdisjoint(production["entrypoints"]))
        self.assertEqual(set(test_mod["entrypoints"]), test_keys)
        for class_names in test_mod["entrypoints"].values():
            for qualified_name in class_names:
                source = (
                    root / "guest/src/gametest/java" / (qualified_name.replace(".", "/") + ".java")
                )
                self.assertTrue(source.is_file(), qualified_name)
