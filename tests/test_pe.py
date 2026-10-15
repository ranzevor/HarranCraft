from __future__ import annotations

import hashlib
import io
import os
import struct
import tempfile
import tracemalloc
import unittest
from pathlib import Path
from unittest.mock import patch

from fixtures import image_bytes

from harrancraft_tools.pe import (
    CHUNK_BYTES,
    MAX_FILE_BYTES,
    MAX_METADATA_BYTES,
    Image,
    InspectionError,
    Reader,
    inspect_file,
)


class PEInspectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "probe.dll"

    def inspect(self, data: bytes, **kwargs) -> dict:
        self.path.write_bytes(data)
        return inspect_file(self.path, **kwargs)

    def test_metadata_and_hash(self) -> None:
        data = image_bytes()
        result = self.inspect(data)
        self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(result["file_size"], len(data))
        self.assertEqual(result["kind"], "dll")
        self.assertEqual(result["machine"], "amd64")
        self.assertEqual(result["file_versions"], ["1.55.0.0"])
        self.assertEqual(result["export_count"], 2)
        self.assertEqual(result["named_export_count"], 2)
        self.assertNotIn("exports", result)
        self.assertNotIn("compatible", result)
        self.assertEqual(self.path.read_bytes(), data)

    def test_executable_without_exports_or_version(self) -> None:
        result = self.inspect(
            image_bytes(dll=False, exports=False, version=False), include_exports=True
        )
        self.assertEqual(result["kind"], "exe")
        self.assertEqual(result["exports"], [])
        self.assertEqual(result["file_versions"], [])
        self.assertEqual(result["entrypoint_rva"], 0x1000)

    def test_named_and_forwarded_exports(self) -> None:
        result = self.inspect(image_bytes(), include_exports=True)
        self.assertEqual(
            result["exports"],
            [
                {"ordinal": 1, "rva": 0x1000, "names": ["hc_probe"], "forwarder": None},
                {"ordinal": 2, "rva": 0x2140, "names": ["hc_sleep"], "forwarder": "KERNEL32.Sleep"},
            ],
        )

    def test_ordinal_only_export(self) -> None:
        data = image_bytes()
        struct.pack_into("<I", data, 0x418, 1)
        result = self.inspect(data, include_exports=True)
        self.assertEqual(result["exports"][1]["names"], [])
        self.assertEqual(result["named_export_count"], 1)

    def test_multiple_names_can_alias_one_ordinal(self) -> None:
        data = image_bytes()
        struct.pack_into("<H", data, 0x472, 0)
        result = self.inspect(data, include_exports=True)
        self.assertEqual(result["exports"][0]["names"], ["hc_probe", "hc_sleep"])

    def test_empty_export_slots_are_not_functions(self) -> None:
        data = image_bytes()
        struct.pack_into("<I", data, 0x418, 1)
        struct.pack_into("<I", data, 0x454, 0)
        self.assertEqual(self.inspect(data)["export_count"], 1)

    def test_rejects_header_mutations(self) -> None:
        cases = [
            (0x3C, "<I", 10, "invalid_pe"),
            (0x3C, "<I", 0xFFFFFFF0, "invalid_pe"),
            (0x84, "<H", 0x014C, "unsupported_machine"),
            (0x86, "<H", 0, "invalid_pe"),
            (0x86, "<H", 97, "invalid_pe"),
            (0x94, "<H", 64, "invalid_pe"),
            (0x96, "<H", 0x2000, "invalid_pe"),
            (0x98, "<H", 0x10B, "unsupported_format"),
            (0x98 + 16, "<I", 0x9000, "invalid_pe"),
            (0x98 + 60, "<I", 0x100, "invalid_pe"),
            (0x98 + 56, "<I", 0x100, "invalid_pe"),
            (0x98 + 108, "<I", 1000, "invalid_pe"),
            (0x98 + 112, "<I", 0, "invalid_pe"),
        ]
        for offset, fmt, value, code in cases:
            with self.subTest(offset=offset, value=value):
                data = image_bytes()
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(InspectionError) as caught:
                    self.inspect(data)
                self.assertEqual(caught.exception.code, code)

    def test_rejects_every_truncated_header_prefix(self) -> None:
        data = image_bytes()
        for end in range(0x200):
            with self.subTest(end=end), self.assertRaises(InspectionError):
                self.inspect(data[:end])

    def test_signatures(self) -> None:
        for offset in (0, 0x80):
            data = image_bytes()
            data[offset] = 0
            with self.subTest(offset=offset), self.assertRaises(InspectionError):
                self.inspect(data)

    def test_section_overlap_and_file_bounds(self) -> None:
        second = 0x98 + 240 + 40
        for offset, value, code in (
            (second + 12, 0x1000, "ambiguous_sections"),
            (second + 20, 0x200, "ambiguous_sections"),
            (second + 20, 0x100, "invalid_pe"),
            (second + 20, 0xDFF, "invalid_pe"),
        ):
            data = image_bytes()
            struct.pack_into("<I", data, offset, value)
            with (
                self.subTest(offset=offset, value=value),
                self.assertRaises(InspectionError) as caught,
            ):
                self.inspect(data)
            self.assertEqual(caught.exception.code, code)

    def test_virtual_zero_fill_is_not_file_backing(self) -> None:
        data = image_bytes()
        struct.pack_into("<I", data, 0x98 + 240 + 40 + 8, 0x1000)
        struct.pack_into("<I", data, 0x460, 0x2A00)
        with self.assertRaises(InspectionError) as caught:
            self.inspect(data)
        self.assertEqual(caught.exception.code, "unbacked_rva")

    def test_export_failures(self) -> None:
        cases = [
            (0x414, "<I", 65537, "metadata_limit"),
            (0x418, "<I", 65537, "metadata_limit"),
            (0x470, "<H", 2, "invalid_exports"),
            (0x450, "<I", 0, "invalid_exports"),
            (0x450, "<I", 0x9000, "invalid_exports"),
            (0x464, "<I", 0x20A0, "invalid_exports"),
            (0x4A0, "<B", 27, "invalid_exports"),
            (0x410, "<I", 0xFFFFFFFF, "invalid_exports"),
        ]
        for offset, fmt, value, code in cases:
            data = image_bytes()
            struct.pack_into(fmt, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(InspectionError) as caught:
                self.inspect(data)
            self.assertEqual(caught.exception.code, code)

    def test_forwarder_string_must_end_inside_export_directory(self) -> None:
        data = image_bytes()
        data[0x540:0x580] = b"x" * 0x40
        with self.assertRaises(InspectionError) as caught:
            self.inspect(data)
        self.assertEqual(caught.exception.code, "invalid_exports")

    def test_strings_cannot_cross_section_end(self) -> None:
        data = image_bytes()
        struct.pack_into("<I", data, 0x460, 0x27FF)
        data[0xBFF] = ord("x")
        with self.assertRaises(InspectionError):
            self.inspect(data)

    def test_resource_failures(self) -> None:
        cases = [
            (0xC14, "<I", 0x80000000, "invalid_resources"),
            (0xC14, "<I", 24, "invalid_resources"),
            (0xC0E, "<H", 5000, "metadata_limit"),
            (0xC80, "<H", 512, "invalid_resources"),
            (0xC82, "<H", 51, "invalid_resources"),
            (0xCA8, "<I", 0, "invalid_resources"),
        ]
        for offset, fmt, value, code in cases:
            data = image_bytes()
            struct.pack_into(fmt, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(InspectionError) as caught:
                self.inspect(data)
            self.assertEqual(caught.exception.code, code)

    def test_version_without_fixed_value(self) -> None:
        data = image_bytes()
        struct.pack_into("<H", data, 0xC82, 0)
        self.assertEqual(self.inspect(data)["file_versions"], [])

    def test_missing_and_directory_inputs(self) -> None:
        for path, code in (
            (self.path, "input_unreadable"),
            (Path(self.temp.name), "not_regular_file"),
        ):
            with self.subTest(code=code), self.assertRaises(InspectionError) as caught:
                inspect_file(path)
            self.assertEqual(caught.exception.code, code)

    @unittest.skipUnless(hasattr(os, "mkfifo"), "POSIX FIFO input")
    def test_fifo_rejected_before_open(self) -> None:
        os.mkfifo(self.path)
        with self.assertRaises(InspectionError) as caught:
            inspect_file(self.path)
        self.assertEqual(caught.exception.code, "not_regular_file")

    def test_oversized_sparse_file(self) -> None:
        with self.path.open("wb") as stream:
            stream.truncate(MAX_FILE_BYTES + 1)
        with self.assertRaises(InspectionError) as caught:
            inspect_file(self.path)
        self.assertEqual(caught.exception.code, "file_limit")

    def test_reader_budget(self) -> None:
        reader = Reader(io.BytesIO(b"abc"), 3)
        reader.bytes_read = MAX_METADATA_BYTES
        with self.assertRaises(InspectionError) as caught:
            reader.read(0, 1)
        self.assertEqual(caught.exception.code, "metadata_limit")

    def test_short_read_is_input_change(self) -> None:
        with self.assertRaises(InspectionError) as caught:
            Reader(io.BytesIO(b"a"), 3).read(0, 3)
        self.assertEqual(caught.exception.code, "input_changed")

    def test_concurrent_change_is_rejected(self) -> None:
        self.path.write_bytes(image_bytes())
        original = Image.versions

        def mutate(image):
            result = original(image)
            with self.path.open("ab") as stream:
                stream.write(b"changed")
            return result

        with patch.object(Image, "versions", mutate), self.assertRaises(InspectionError) as caught:
            inspect_file(self.path)
        self.assertEqual(caught.exception.code, "input_changed")

    def test_hashing_memory_does_not_scale_with_file_size(self) -> None:
        data = image_bytes()
        digest = hashlib.sha256(data)
        padding = bytes(CHUNK_BYTES)
        with self.path.open("wb") as stream:
            stream.write(data)
            for _ in range(32):
                stream.write(padding)
                digest.update(padding)
        tracemalloc.start()
        try:
            result = inspect_file(self.path)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertEqual(result["sha256"], digest.hexdigest())
        self.assertLess(peak, 6 * CHUNK_BYTES)


if __name__ == "__main__":
    unittest.main()
