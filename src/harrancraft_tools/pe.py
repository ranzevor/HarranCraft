"""Bounded inspection of file-backed AMD64 PE32+ metadata."""

from __future__ import annotations

import hashlib
import os
import stat
import struct
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import BinaryIO

MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_METADATA_BYTES = 8 * 1024 * 1024
MAX_EXPORTS = 65536
MAX_STRING_BYTES = 4096
MAX_RESOURCE_ENTRIES = 4096
CHUNK_BYTES = 1024 * 1024
U32_LIMIT = 1 << 32


class InspectionError(Exception):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class Section:
    rva: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int


@dataclass(frozen=True)
class Export:
    ordinal: int
    rva: int
    names: tuple[str, ...]
    forwarder: str | None


class Reader:
    def __init__(self, stream: BinaryIO, size: int) -> None:
        self.stream = stream
        self.size = size
        self.bytes_read = 0

    def read(self, offset: int, count: int) -> bytes:
        if offset < 0 or count < 0 or offset > self.size or count > self.size - offset:
            raise InspectionError("invalid_pe")
        if count > MAX_METADATA_BYTES - self.bytes_read:
            raise InspectionError("metadata_limit")
        self.bytes_read += count
        self.stream.seek(offset)
        data = self.stream.read(count)
        if len(data) != count:
            raise InspectionError("input_changed")
        return data

    def unpack(self, fmt: str, offset: int) -> tuple:
        return struct.unpack(fmt, self.read(offset, struct.calcsize(fmt)))


class Image:
    def __init__(self, reader: Reader) -> None:
        self.reader = reader
        self.sections: list[Section] = []
        self.directories: list[tuple[int, int]] = []
        self.headers_size = 0
        self.image_size = 0
        self.resource_entries = 0
        self._headers()

    def _headers(self) -> None:
        r = self.reader
        if r.read(0, 2) != b"MZ":
            raise InspectionError("invalid_pe")
        pe = r.unpack("<I", 0x3C)[0]
        if pe < 64 or r.read(pe, 4) != b"PE\0\0":
            raise InspectionError("invalid_pe")
        machine, count, self.timestamp, _, _, optional_size, self.flags = r.unpack(
            "<HHIIIHH", pe + 4
        )
        if machine != 0x8664:
            raise InspectionError("unsupported_machine")
        if not 1 <= count <= 96 or optional_size < 112 or not self.flags & 0x0002:
            raise InspectionError("invalid_pe")
        optional = pe + 24
        data = r.read(optional, optional_size)
        if struct.unpack_from("<H", data)[0] != 0x20B:
            raise InspectionError("unsupported_format")
        self.entrypoint = struct.unpack_from("<I", data, 16)[0]
        self.image_size, self.headers_size = struct.unpack_from("<II", data, 56)
        self.subsystem = struct.unpack_from("<H", data, 68)[0]
        directory_count = struct.unpack_from("<I", data, 108)[0]
        if directory_count > (optional_size - 112) // 8:
            raise InspectionError("invalid_pe")
        self.directories = [
            struct.unpack_from("<II", data, 112 + i * 8) for i in range(directory_count)
        ]
        section_offset = optional + optional_size
        if not (
            section_offset + count * 40 <= self.headers_size <= r.size
            and self.headers_size <= self.image_size < U32_LIMIT
        ):
            raise InspectionError("invalid_pe")
        raw_ranges: list[tuple[int, int]] = []
        virtual_ranges: list[tuple[int, int]] = []
        for i in range(count):
            section = r.read(section_offset + i * 40, 40)
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", section, 8)
            characteristics = struct.unpack_from("<I", section, 36)[0]
            span = max(raw_size, virtual_size)
            if span:
                if rva < self.headers_size or rva + span > self.image_size:
                    raise InspectionError("invalid_pe")
                virtual_ranges.append((rva, rva + span))
            if raw_size:
                if raw_offset < self.headers_size or raw_offset + raw_size > r.size:
                    raise InspectionError("invalid_pe")
                raw_ranges.append((raw_offset, raw_offset + raw_size))
            self.sections.append(Section(rva, virtual_size, raw_offset, raw_size, characteristics))
        for ranges in (raw_ranges, virtual_ranges):
            ranges.sort()
            if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:], strict=False)):
                raise InspectionError("ambiguous_sections")
        if self.entrypoint >= self.image_size:
            raise InspectionError("invalid_pe")

    def _backing(self, rva: int) -> tuple[int, int]:
        if rva < 0 or rva >= U32_LIMIT:
            raise InspectionError("invalid_pe")
        if rva < self.headers_size:
            return rva, self.headers_size - rva
        for s in self.sections:
            if s.rva <= rva < s.rva + s.raw_size:
                delta = rva - s.rva
                return s.raw_offset + delta, s.raw_size - delta
        raise InspectionError("unbacked_rva")

    def at(self, rva: int, count: int) -> bytes:
        offset, available = self._backing(rva)
        if count < 0 or count > available:
            raise InspectionError("unbacked_rva")
        return self.reader.read(offset, count)

    def string(self, rva: int, end_rva: int | None = None) -> str:
        offset, available = self._backing(rva)
        if end_rva is not None:
            available = min(available, end_rva - rva)
        limit = min(available, MAX_STRING_BYTES)
        if limit <= 0:
            raise InspectionError("invalid_exports")
        parts = bytearray()
        for start in range(0, limit, 64):
            chunk = self.reader.read(offset + start, min(64, limit - start))
            terminator = chunk.find(b"\0")
            parts.extend(chunk if terminator < 0 else chunk[:terminator])
            if terminator >= 0:
                if not parts or any(c < 32 or c > 126 for c in parts):
                    raise InspectionError("invalid_exports")
                return parts.decode("ascii")
        raise InspectionError("invalid_exports")

    def directory(self, index: int) -> tuple[int, int]:
        if index >= len(self.directories):
            return 0, 0
        rva, size = self.directories[index]
        if bool(rva) != bool(size) or rva + size > self.image_size:
            raise InspectionError("invalid_pe")
        return rva, size

    def exports(self) -> list[Export]:
        rva, size = self.directory(0)
        if not rva:
            return []
        if size < 40:
            raise InspectionError("invalid_exports")
        self.at(rva, size)
        fields = struct.unpack("<IIHHIIIIIII", self.at(rva, 40))
        _, _, _, _, dll_name, base, function_count, name_count, functions, names, ordinals = fields
        if function_count > MAX_EXPORTS or name_count > MAX_EXPORTS:
            raise InspectionError("metadata_limit")
        if base + function_count > U32_LIMIT or (name_count and not function_count):
            raise InspectionError("invalid_exports")
        if dll_name:
            self.string(dll_name)
        addresses = (
            struct.unpack(f"<{function_count}I", self.at(functions, function_count * 4))
            if function_count else ()
        )
        name_rvas = struct.unpack(f"<{name_count}I", self.at(names, name_count * 4)) if name_count else ()
        indices = struct.unpack(f"<{name_count}H", self.at(ordinals, name_count * 2)) if name_count else ()
        aliases: dict[int, list[str]] = {}
        seen: set[str] = set()
        for name_rva, index in zip(name_rvas, indices, strict=True):
            if index >= function_count or not addresses[index]:
                raise InspectionError("invalid_exports")
            name = self.string(name_rva)
            if name in seen:
                raise InspectionError("invalid_exports")
            seen.add(name)
            aliases.setdefault(index, []).append(name)
        result = []
        for index, address in enumerate(addresses):
            if not address:
                continue
            if address >= self.image_size:
                raise InspectionError("invalid_exports")
            forwarder = self.string(address, rva + size) if rva <= address < rva + size else None
            result.append(Export(base + index, address, tuple(aliases.get(index, [])), forwarder))
        return result

    def versions(self) -> list[str]:
        root, size = self.directory(2)
        if not root:
            return []
        if size < 16:
            raise InspectionError("invalid_resources")
        found: set[str] = set()
        visited: set[int] = set()

        def read(relative: int, length: int) -> bytes:
            if relative < 0 or length > size - relative:
                raise InspectionError("invalid_resources")
            return self.at(root + relative, length)

        def walk(relative: int, depth: int) -> None:
            if relative in visited or depth > 2:
                raise InspectionError("invalid_resources")
            visited.add(relative)
            named, ids = struct.unpack_from("<HH", read(relative, 16), 12)
            count = named + ids
            self.resource_entries += count
            if self.resource_entries > MAX_RESOURCE_ENTRIES:
                raise InspectionError("metadata_limit")
            entries = read(relative + 16, count * 8)
            for i in range(count):
                name, target = struct.unpack_from("<II", entries, i * 8)
                if depth == 0 and name != 16:
                    continue
                is_directory = bool(target & 0x80000000)
                target &= 0x7FFFFFFF
                if depth < 2:
                    if not is_directory:
                        raise InspectionError("invalid_resources")
                    walk(target, depth + 1)
                else:
                    if is_directory:
                        raise InspectionError("invalid_resources")
                    data_rva, data_size, _, _ = struct.unpack("<IIII", read(target, 16))
                    version = self._fixed_version(data_rva, data_size)
                    if version is not None:
                        found.add(version)
        walk(0, 0)
        return sorted(found)

    def _fixed_version(self, rva: int, size: int) -> str | None:
        if size < 6:
            raise InspectionError("invalid_resources")
        data = self.at(rva, min(size, 128))
        length, value_length, value_type = struct.unpack_from("<HHH", data)
        key = "VS_VERSION_INFO\0".encode("utf-16-le")
        value_offset = (6 + len(key) + 3) & ~3
        if length > size or length < value_offset or data[6:6 + len(key)] != key or value_type != 0:
            raise InspectionError("invalid_resources")
        if not value_length:
            return None
        if value_length != 52 or value_offset + value_length > length:
            raise InspectionError("invalid_resources")
        value = self.at(rva + value_offset, 52)
        signature, _, ms, ls = struct.unpack_from("<IIII", value)
        if signature != 0xFEEF04BD:
            raise InspectionError("invalid_resources")
        return f"{ms >> 16}.{ms & 65535}.{ls >> 16}.{ls & 65535}"


def _stamp(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns


def inspect_file(path: Path, *, include_exports: bool = False) -> dict:
    """Read an explicit regular file; no loading, discovery, or compatibility verdict."""
    fd = -1
    try:
        initial = path.stat()
        if not stat.S_ISREG(initial.st_mode):
            raise InspectionError("not_regular_file")
        flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
        fd = os.open(path, flags)
        with os.fdopen(fd, "rb") as stream:
            fd = -1
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise InspectionError("not_regular_file")
            if before.st_size > MAX_FILE_BYTES:
                raise InspectionError("file_limit")
            image = Image(Reader(stream, before.st_size))
            exports = image.exports()
            versions = image.versions()
            digest = hashlib.sha256()
            stream.seek(0)
            remaining = before.st_size
            while remaining:
                chunk = stream.read(min(CHUNK_BYTES, remaining))
                if not chunk:
                    raise InspectionError("input_changed")
                digest.update(chunk)
                remaining -= len(chunk)
            if stream.read(1) or _stamp(before) != _stamp(os.fstat(stream.fileno())):
                raise InspectionError("input_changed")
            result = {
                "sha256": digest.hexdigest(),
                "file_size": before.st_size,
                "machine": "amd64",
                "format": "PE32+",
                "kind": "dll" if image.flags & 0x2000 else "exe",
                "coff_timestamp": image.timestamp,
                "image_size": image.image_size,
                "entrypoint_rva": image.entrypoint,
                "subsystem": image.subsystem,
                "file_versions": versions,
                "sections": [asdict(s) for s in image.sections],
                "export_count": len(exports),
                "named_export_count": sum(len(e.names) for e in exports),
            }
            if include_exports:
                result["exports"] = [dict(asdict(e), names=list(e.names)) for e in exports]
            return result
    except InspectionError:
        raise
    except (OSError, ValueError) as exc:
        raise InspectionError("input_unreadable") from exc
    finally:
        if fd >= 0:
            os.close(fd)
