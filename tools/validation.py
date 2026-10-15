"""Source identity, stage reports and evidence for the existing validation commands."""

from __future__ import annotations

import hashlib
import json
import math
import os
import platform
import re
import shutil
import signal
import subprocess
import time
import uuid
from datetime import UTC, datetime
from pathlib import Path

if __package__:
    from .processes import execute, write_json
else:
    from processes import execute, write_json

MAX_SOURCE_FILES = 4096
MAX_SOURCE_BYTES = 16 * 1024 * 1024
MAX_EVIDENCE_FILES = 512
MAX_EVIDENCE_BYTES = 128 * 1024 * 1024
SCREENSHOTS = {f"harrancraft-h2a-{state}.png" for state in ("empty", "placed", "removed")}


class Halt(Exception):
    pass


def options(arguments: list[str], default_timeout: int) -> tuple[dict, list[str]]:
    result = {"preflight": False, "timeout": default_timeout, "revision": None}
    forwarded = []
    seen = set()
    iterator = iter(arguments)
    for argument in iterator:
        if not argument.startswith("--hc-"):
            forwarded.append(argument)
            continue
        if argument in seen:
            raise ValueError("duplicate_validation_option")
        seen.add(argument)
        if argument == "--hc-preflight":
            result["preflight"] = True
        elif argument in {"--hc-timeout", "--hc-revision"}:
            value = next(iterator, None)
            if value is None:
                raise ValueError("missing_validation_option_value")
            if argument == "--hc-timeout":
                try:
                    duration = float(value)
                except ValueError:
                    raise ValueError("invalid_validation_timeout") from None
                if not math.isfinite(duration) or not 1 <= duration <= 86400:
                    raise ValueError("invalid_validation_timeout")
                result["timeout"] = duration
            elif not re.fullmatch(r"[0-9a-f]{40}", value):
                raise ValueError("invalid_expected_revision")
            else:
                result["revision"] = value
        else:
            raise ValueError("unknown_validation_option")
    return result, forwarded


def safe_directory(root: Path, parts: tuple[str, ...]) -> Path:
    path = root
    for part in parts:
        path = path / part
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError("output_link_rejected")
        path.mkdir(mode=0o700, exist_ok=True)
        if not path.is_dir():
            raise ValueError("output_directory_required")
    return path


def git(root: Path, *arguments: str, timeout: float = 10) -> bytes:
    command = ["git", "-c", "core.fsmonitor=false", "-C", str(root), *arguments]
    env = {
        key: value
        for key, value in os.environ.items()
        if key
        not in {
            "GIT_DIR",
            "GIT_WORK_TREE",
            "GIT_INDEX_FILE",
            "GIT_COMMON_DIR",
        }
    }
    result = subprocess.run(
        command,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        timeout=timeout,
        check=False,
        env=env,
    )
    if result.returncode or len(result.stdout) > 2 * 1024 * 1024:
        raise ValueError("source_identity_unavailable")
    return result.stdout


def digest(path: Path, maximum: int, deadline: float | None = None) -> str:
    total = 0
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError("file_read_deadline_exceeded")
            total += len(chunk)
            if total > maximum:
                raise ValueError("file_size_limit_exceeded")
            value.update(chunk)
    return value.hexdigest()


def source_identity(root: Path, deadline: float | None = None) -> dict:
    def read(*arguments: str) -> bytes:
        remaining = 10 if deadline is None else min(10, deadline - time.monotonic())
        if remaining <= 0:
            raise TimeoutError("source_identity_deadline_exceeded")
        return git(root, *arguments, timeout=remaining)

    if Path(os.fsdecode(read("rev-parse", "--show-toplevel")).strip()).resolve() != root.resolve():
        raise ValueError("source_root_mismatch")
    revision = read("rev-parse", "HEAD").decode("ascii").strip()
    tree = read("rev-parse", "HEAD^{tree}").decode("ascii").strip()
    if not all(re.fullmatch(r"[0-9a-f]{40}", value) for value in (revision, tree)):
        raise ValueError("unsupported_git_object_format")
    names = sorted(
        set(read("ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0"))
        - {b""}
    )
    if len(names) > MAX_SOURCE_FILES:
        raise ValueError("source_file_limit_exceeded")
    identity = hashlib.sha256()
    for raw_name in names:
        name = os.fsdecode(raw_name)
        path = root / name
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise ValueError("source_link_rejected")
        identity.update(raw_name + b"\0")
        if not path.exists():
            identity.update(b"deleted\0")
        elif not path.is_file():
            raise ValueError("source_regular_file_required")
        else:
            identity.update(str(path.stat().st_mode & 0o111).encode("ascii") + b"\0")
            identity.update(digest(path, MAX_SOURCE_BYTES, deadline).encode("ascii") + b"\0")
    return {
        "revision": revision,
        "tree": tree,
        "file_count": len(names),
        "dirty": bool(read("status", "--porcelain=v1", "-z", "--untracked-files=all")),
        "snapshot_sha256": identity.hexdigest(),
    }


def evidence_snapshot(root: Path) -> dict[str, tuple[int, int, str]]:
    base = root / "guest" / "build"
    result = {}
    if not base.exists():
        return result
    if base.is_symlink() or not base.resolve().is_relative_to(root.resolve()):
        raise ValueError("evidence_link_rejected")
    total = 0
    visited = 0

    def failed(error: OSError) -> None:
        raise ValueError("evidence_enumeration_failed") from error

    for directory, directories, filenames in os.walk(base, onerror=failed):
        parent = Path(directory)
        # World data, caches and compiled classes are not validation evidence.
        directories[:] = [
            name
            for name in directories
            if name
            not in {
                "world",
                "saves",
                "assets",
                "libraries",
                "classes",
                "resources",
                "tmp",
                ".gradle",
            }
        ]
        for name in directories:
            if (parent / name).is_symlink() or not (parent / name).resolve().is_relative_to(
                base.resolve()
            ):
                raise ValueError("evidence_link_rejected")
        visited += len(directories) + len(filenames)
        if visited > 8192:
            raise ValueError("evidence_traversal_limit_exceeded")
        for name in filenames:
            path = parent / name
            if path.suffix not in {".png", ".xml", ".log"}:
                continue
            if path.is_symlink() or not path.resolve().is_relative_to(base.resolve()):
                raise ValueError("evidence_link_rejected")
            if not path.is_file():
                raise ValueError("evidence_regular_file_required")
            stat = path.stat()
            total += stat.st_size
            if len(result) >= MAX_EVIDENCE_FILES or total > MAX_EVIDENCE_BYTES:
                raise ValueError("evidence_limit_exceeded")
            result[path.relative_to(root).as_posix()] = (
                stat.st_mtime_ns,
                stat.st_size,
                digest(path, MAX_EVIDENCE_BYTES),
            )
    return result


def collect_evidence(root: Path, directory: Path, before: dict, *, screenshots: bool) -> dict:
    after = evidence_snapshot(root)
    copied = []
    found = set()
    for name, identity in after.items():
        if before.get(name) == identity:
            continue
        source = root / name
        target = directory / "artifacts" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if digest(target, MAX_EVIDENCE_BYTES) != identity[2]:
            raise ValueError("evidence_changed_during_collection")
        copied.append(
            {
                "path": target.relative_to(directory).as_posix(),
                "bytes": identity[1],
                "sha256": identity[2],
            }
        )
        screenshot_name = re.sub(r"^\d{4,}_", "", source.name)
        if screenshot_name in SCREENSHOTS:
            with target.open("rb") as stream:
                header = stream.read(24)
            if (
                len(header) != 24
                or header[:8] != b"\x89PNG\r\n\x1a\n"
                or header[12:16] != b"IHDR"
                or int.from_bytes(header[16:20], "big") == 0
                or int.from_bytes(header[20:24], "big") == 0
            ):
                raise ValueError("screenshot_header_invalid")
            found.add(screenshot_name)
    missing = sorted(SCREENSHOTS - found) if screenshots else []
    return {
        "status": "failed" if missing else "passed",
        "files": copied,
        "missing_screenshots": missing,
        "screenshot_check": "signature_and_dimensions",
        "visual_review": "not_run",
    }


class Validation:
    def __init__(self, root: Path, component: str, settings: dict, stages: list[str]) -> None:
        self.root = root.resolve()
        self.settings = settings
        self.directory = safe_directory(self.root, ("reports", "validation")) / uuid.uuid4().hex
        self.directory.mkdir(mode=0o700)
        self.path = self.directory / "result.json"
        self.deadline = time.monotonic() + settings["timeout"]
        self.lock = None
        self.report = {
            "schema_version": 1,
            "component": component,
            "status": "running",
            "started_at": datetime.now(UTC).isoformat(),
            "platform": {
                "system": platform.system(),
                "machine": platform.machine(),
                "python": platform.python_version(),
            },
            "source": None,
            "tools": {},
            "preflight": "not_run",
            "stages": [{"name": name, "status": "not_run"} for name in stages],
        }
        self.save()

    def save(self) -> None:
        write_json(self.path, self.report)

    def reject(self, reason: str, status: str = "blocked") -> None:
        self.report.update(status=status, reason=reason)
        if self.report["preflight"] == "running":
            self.report["preflight"] = status
        self.save()
        raise Halt(reason)

    def preflight(self, tools: tuple[str, ...]) -> None:
        self.report["preflight"] = "running"
        if tuple(map(int, self.report["platform"]["python"].split(".")[:2])) < (3, 12):
            self.reject("python_version_unsupported")
        try:
            self.report["source"] = source_identity(self.root, self.deadline)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            self.report["source_error_type"] = type(error).__name__
            if isinstance(error, ValueError):
                self.report["source_error"] = str(error)
            self.reject("source_identity_unavailable")
        expected = self.settings["revision"]
        if expected and self.report["source"]["revision"] != expected:
            self.reject("source_revision_mismatch")
        for tool in tools:
            if time.monotonic() >= self.deadline:
                self.reject("preflight_deadline_exceeded", "timed_out")
            executable = shutil.which(tool)
            if executable is None:
                self.report["tools"][tool] = {"status": "missing"}
                self.reject("required_tool_missing")
            argument = "-version" if tool in {"java", "javac"} else "--version"
            result = execute(
                [executable, argument],
                self.root,
                self.directory,
                "probe-" + tool,
                min(15, max(0.01, self.deadline - time.monotonic())),
            )
            text = "".join(
                (self.directory / result[key]).read_text(encoding="utf-8", errors="replace")
                for key in ("stdout", "stderr")
                if key in result
            )
            patterns = {
                "java": r'(?:openjdk|java) (?:version )?"?([0-9]+(?:\.[0-9]+)*)',
                "javac": r"javac ([0-9]+(?:\.[0-9]+)*)",
                "cmake": r"cmake version ([0-9]+(?:\.[0-9]+)*)",
                "ctest": r"ctest version ([0-9]+(?:\.[0-9]+)*)",
            }
            match = re.search(patterns[tool], text)
            version = match.group(1) if match else None
            self.report["tools"][tool] = {
                "status": result["status"],
                "version": version,
                "cleanup": result["cleanup"],
            }
            if result["status"] != "passed" or version is None:
                self.reject("required_tool_probe_failed")
            numbers = tuple(map(int, version.split(".")))
            if tool in {"java", "javac"} and numbers[0] != 21:
                self.reject("jdk_version_unsupported")
            if tool in {"cmake", "ctest"} and numbers[:2] < (3, 25):
                self.reject("cmake_version_unsupported")
        self.report["preflight"] = "passed"
        self.save()

    def acquire(self) -> None:
        base = safe_directory(self.root, ("build",))
        lock = base / ".validation-lock"
        try:
            lock.mkdir(mode=0o700)
        except FileExistsError:
            self.reject("validation_lock_exists")
        self.lock = lock
        (lock / "owner").write_text(self.directory.name, encoding="ascii")

    def run(
        self,
        name: str,
        command: list[str],
        limit: float = 120,
        *,
        env: dict[str, str] | None = None,
    ) -> str:
        stage = next(value for value in self.report["stages"] if value["name"] == name)
        if stage["status"] != "not_run":
            self.reject("stage_already_executed", "failed")
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            stage.update(status="timed_out", reason="deadline_exceeded", cleanup="not_run")
        else:
            stage["status"] = "running"
            self.save()
            stage.update(
                execute(command, self.root, self.directory, name, min(limit, remaining), env=env)
            )
        self.save()
        if stage["status"] != "passed":
            self.report.update(status=stage["status"], failed_stage=name)
            raise Halt(name)
        return (self.directory / stage["stdout"]).read_text(encoding="utf-8", errors="replace")

    def finish(self) -> int:
        try:
            initial = self.report["source"]
            if initial is not None:
                final = source_identity(self.root, time.monotonic() + 10)
                self.report["source_after"] = final
                if final != initial:
                    self.report.update(status="failed", reason="source_changed_during_validation")
            if self.report["status"] == "running":
                if self.settings["preflight"]:
                    self.report["status"] = "preflight_passed"
                else:
                    self.report["status"] = (
                        "passed"
                        if all(stage["status"] == "passed" for stage in self.report["stages"])
                        else "incomplete"
                    )
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            self.report.update(
                status="failed",
                reason="source_finalization_failed",
                error_type=type(error).__name__,
            )
        finally:
            if self.lock is not None:
                try:
                    owner = self.lock / "owner"
                    if (
                        self.lock.is_symlink()
                        or owner.read_text(encoding="ascii") != self.directory.name
                    ):
                        raise ValueError("validation_lock_changed")
                    owner.unlink()
                    self.lock.rmdir()
                except (OSError, ValueError) as error:
                    self.report.update(
                        status="failed",
                        reason="validation_lock_cleanup_failed",
                        cleanup_error_type=type(error).__name__,
                    )
            self.report["finished_at"] = datetime.now(UTC).isoformat()
            self.save()
        print(
            json.dumps(
                {
                    "status": self.report["status"],
                    "report": self.path.relative_to(self.root).as_posix(),
                },
                sort_keys=True,
            )
        )
        return 0 if self.report["status"] in {"passed", "preflight_passed"} else 1


def entry(
    root: Path,
    component: str,
    arguments: list[str],
    default_timeout: int,
    stages: list[str],
    action,
) -> int:
    try:
        settings, forwarded = options(arguments, default_timeout)
        validation = Validation(root, component, settings, stages)
    except (OSError, ValueError) as error:
        print(
            json.dumps(
                {
                    "status": "blocked",
                    "error_type": type(error).__name__,
                    "reason": str(error) if isinstance(error, ValueError) else "output_unavailable",
                }
            )
        )
        return 2
    old_handler = signal.getsignal(signal.SIGTERM)

    def interrupt(signum: int, frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupt)
    try:
        action(validation, forwarded)
    except Halt:
        pass
    except KeyboardInterrupt:
        validation.report.update(status="interrupted", reason="interrupted")
    except Exception as error:
        import traceback

        with (validation.directory / "failure.log").open("w", encoding="utf-8") as stream:
            traceback.print_exc(file=stream)
        validation.report.update(
            status="failed", error_type=type(error).__name__, diagnostic="failure.log"
        )
    finally:
        signal.signal(signal.SIGTERM, old_handler)
    return validation.finish()
