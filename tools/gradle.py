"""Run the checksum-pinned Gradle distribution in the repository's build directory."""

from __future__ import annotations

import hashlib
import os
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
VERSION = "8.14.3"
SHA256 = "bd71102213493060956ec229d946beee57158dbd89d0e62b91bca0fa2c5f3531"
URL = f"https://downloads.gradle.org/distributions/gradle-{VERSION}-bin.zip"
MAX_DOWNLOAD = 256 * 1024 * 1024
MAX_EXPANDED = 512 * 1024 * 1024
MAX_ENTRIES = 8192


def unpack(archive: Path, staging: Path, home_name: str) -> Path:
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        if len(entries) > MAX_ENTRIES or sum(item.file_size for item in entries) > MAX_EXPANDED:
            raise ValueError("Gradle archive exceeds extraction limits")
        seen = set()
        for info in entries:
            if info.orig_filename != info.filename:
                raise ValueError("Unexpected Gradle archive path")
            relative = PurePosixPath(info.filename)
            normalized = relative.as_posix() + ("/" if info.is_dir() else "")
            if (
                relative.is_absolute()
                or not relative.parts
                or relative.parts[0] != home_name
                or ".." in relative.parts
                or "\\" in info.filename
                or normalized != info.filename
            ):
                raise ValueError("Unexpected Gradle archive path")
            identity = relative.as_posix().casefold()
            if identity in seen:
                raise ValueError("Duplicate Gradle archive path")
            seen.add(identity)
            mode = stat.S_IFMT(info.external_attr >> 16)
            if mode not in {0, stat.S_IFREG, stat.S_IFDIR}:
                raise ValueError("Unexpected Gradle archive file type")
        source.extractall(staging)
    executable = staging / home_name / "bin" / ("gradle.bat" if os.name == "nt" else "gradle")
    if not executable.is_file():
        raise ValueError("Gradle archive is missing its launcher")
    if os.name != "nt":
        executable.chmod(0o755)
    return executable


def prepare() -> Path:
    cache = ROOT / "build" / "toolchains"
    home = cache / f"gradle-{VERSION}"
    executable = home / "bin" / ("gradle.bat" if os.name == "nt" else "gradle")
    marker = home / ".harrancraft-source-sha256"
    for path in (ROOT / "build", cache, home, home / "bin", executable, marker):
        if path.is_symlink():
            raise ValueError("Gradle cache must not contain symlinks")
    if home.exists():
        if (
            executable.is_file()
            and marker.is_file()
            and marker.read_text(encoding="ascii") == SHA256
        ):
            return executable
        raise ValueError(
            f"Invalid Gradle cache; remove build/toolchains/gradle-{VERSION} before reinstalling"
        )
    cache.mkdir(parents=True, exist_ok=True)
    lock = cache / ".install-lock"
    try:
        lock.mkdir()
    except FileExistsError:
        raise ValueError(
            "Gradle installation lock exists: build/toolchains/.install-lock"
        ) from None
    try:
        with tempfile.TemporaryDirectory(dir=cache) as temporary:
            staging = Path(temporary)
            archive = staging / "gradle.zip"
            digest = hashlib.sha256()
            total = 0
            with urllib.request.urlopen(URL, timeout=60) as response, archive.open("wb") as stream:
                while chunk := response.read(1024 * 1024):
                    total += len(chunk)
                    if total > MAX_DOWNLOAD:
                        raise ValueError("Gradle archive exceeds download limit")
                    digest.update(chunk)
                    stream.write(chunk)
            if digest.hexdigest() != SHA256:
                raise ValueError("Gradle distribution checksum mismatch")
            unpack(archive, staging, home.name)
            (staging / home.name / marker.name).write_text(SHA256, encoding="ascii")
            if home.exists():
                raise ValueError("Gradle cache changed during installation")
            (staging / home.name).rename(home)
    finally:
        lock.rmdir()
    return executable


def main() -> int:
    executable = prepare()
    return subprocess.run(
        [str(executable), "--no-daemon", "-p", str(ROOT / "guest"), *sys.argv[1:]],
        cwd=ROOT,
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
