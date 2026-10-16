"""Run the checksum-pinned Gradle distribution in the repository's build directory."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

if __package__:
    from .game_scenarios import client_results, lan_requested
    from .validation import Validation, collect_evidence, digest, entry, evidence_snapshot
else:
    from game_scenarios import client_results, lan_requested
    from validation import Validation, collect_evidence, digest, entry, evidence_snapshot

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


def validate(check: Validation, arguments: list[str]) -> None:
    if not arguments:
        arguments = ["build"]
    for argument in arguments:
        if argument in {
            "--project-dir",
            "--init-script",
            "--include-build",
            "-c",
            "--settings-file",
            "-b",
            "--build-file",
        } or argument.startswith(
            (
                "-p",
                "-I",
                "-b",
                "-c",
                "--project-dir=",
                "--init-script=",
                "--include-build=",
                "--settings-file=",
                "--build-file=",
            )
        ):
            check.reject("validation_project_override_rejected")
    if any(value in {"-m", "--dry-run"} for value in arguments):
        check.reject("validation_dry_run_rejected")
    if any(value == "-x" or value.startswith(("-x", "--exclude-task")) for value in arguments):
        check.reject("validation_task_exclusion_rejected")
    allowed_tasks = {"build", "check", "test", "clean", "runGameTest", "runClientGameTest"}
    allowed_flags = {
        "--offline",
        "--refresh-dependencies",
        "--rerun-tasks",
        "--stacktrace",
        "--full-stacktrace",
        "--info",
        "--build-cache",
        "--no-build-cache",
        "--parallel",
        "--no-parallel",
        "--continue",
        "--no-daemon",
        "--console=plain",
    }
    tasks = set()
    supplied = {}
    for argument in arguments:
        task = argument.removeprefix(":")
        if task in allowed_tasks:
            tasks.add(task)
        elif argument.startswith("-P"):
            name, separator, value = argument[2:].partition("=")
            if name not in {
                "runMinecraftTests",
                "acceptMinecraftEula",
                "peerExecutable",
                "runLanTests",
            }:
                check.reject("validation_property_unsupported")
            if name in supplied:
                check.reject("duplicate_validation_property")
            if not separator or not value or any(ord(char) < 32 for char in value):
                check.reject("validation_property_invalid")
            if name != "peerExecutable" and value not in {"true", "false"}:
                check.reject("validation_property_invalid")
            supplied[name] = value
        elif argument not in allowed_flags and not re.fullmatch(
            r"--max-workers=[1-9][0-9]{0,3}", argument
        ):
            check.reject(
                "validation_option_unsupported"
                if argument.startswith("-")
                else "validation_task_unsupported"
            )
    if not tasks or tasks == {"clean"}:
        check.reject("validation_task_unsupported")
    enabled = supplied.get("runMinecraftTests", "false") == "true"
    game = enabled or bool(tasks & {"runGameTest", "runClientGameTest"})
    client = "runClientGameTest" in tasks
    check.report["requested_tasks"] = sorted(tasks)
    check.report["minecraft_execution"] = game
    if game and not (enabled and supplied.get("acceptMinecraftEula") == "true"):
        check.reject("minecraft_opt_in_required")
    try:
        lan = lan_requested(supplied, enabled=enabled, client=client)
    except ValueError as error:
        check.reject(str(error))
    check.report["lan_execution"] = lan
    # Command-line project properties override ambient Gradle properties and environment values.
    arguments = [
        value
        for value in arguments
        if not value.startswith(
            ("-PrunMinecraftTests=", "-PacceptMinecraftEula=", "-PrunLanTests=")
        )
        and value not in {"--no-daemon", "--console=plain"}
    ]
    selected = "true" if enabled else "false"
    arguments.extend([f"-PrunMinecraftTests={selected}", f"-PacceptMinecraftEula={selected}"])
    arguments.append("-PrunLanTests=" + ("true" if lan else "false"))
    check.preflight(("java", "javac"))
    properties = {}
    for line in (ROOT / "guest/gradle.properties").read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            if key.strip() in {
                "minecraft_version",
                "yarn_mappings",
                "loader_version",
                "fabric_version",
            }:
                if not re.fullmatch(r"[0-9][0-9A-Za-z.+_-]{0,99}", value.strip()):
                    check.reject("configured_dependency_invalid")
                properties[key.strip()] = value.strip()
    check.report["configured_dependencies"] = properties | {
        "gradle": VERSION,
        "gradle_sha256": SHA256,
    }
    if client:
        check.report["graphics_session"] = (
            "configured" if os.name == "nt" or os.environ.get("DISPLAY") else "missing"
        )
        if check.report["graphics_session"] == "missing":
            check.reject("client_graphics_session_missing")
        peers = [
            value.partition("=")[2] for value in arguments if value.startswith("-PpeerExecutable=")
        ]
        if len(peers) != 1:
            check.reject("native_peer_required")
        peer = (ROOT / "guest" / peers[0]).resolve()
        if not peer.is_file() or (os.name != "nt" and not os.access(peer, os.X_OK)):
            check.reject("native_peer_unavailable")
        check.report["peer_sha256"] = digest(peer, 32 * 1024 * 1024)
    cache = ROOT / "build/toolchains" / f"gradle-{VERSION}"
    if cache.is_symlink() or not cache.resolve().is_relative_to(ROOT):
        check.reject("gradle_cache_link_rejected")
    check.report["gradle_cache"] = "present" if cache.is_dir() else "missing"
    check.save()
    if check.settings["preflight"]:
        return
    check.acquire()
    before = evidence_snapshot(ROOT)
    try:
        bootstrap = check.run(
            "bootstrap",
            [sys.executable, "-S", str(Path(__file__).resolve()), "--_hc-bootstrap"],
            limit=300,
        )
        executable = json.loads(bootstrap)["executable"]
        check.run(
            "gradle",
            [executable, "-p", str(ROOT / "guest"), *arguments, "--no-daemon", "--console=plain"],
            limit=86400,
        )
    finally:
        stage = next(item for item in check.report["stages"] if item["name"] == "evidence")
        stage["status"] = "running"
        check.save()
        try:
            stage.update(collect_evidence(ROOT, check.directory, before, screenshots=client))
        except (OSError, ValueError) as error:
            stage.update(
                status="failed",
                reason="evidence_collection_failed",
                error_type=type(error).__name__,
            )
        log = check.directory / "gradle.stdout.log"
        if log.is_file():
            with log.open("rb") as stream:
                raw = stream.read(32 * 1024 * 1024 + 1)
            if len(raw) > 32 * 1024 * 1024:
                raise ValueError("gradle_log_scan_limit_exceeded")
            text = raw.decode("utf-8", errors="replace")
            check.report["gradle_task_results"] = [
                {"task": task, "outcome": outcome or "executed"}
                for task, outcome in re.findall(
                    r"(?m)^> Task (:[^\s]+)(?: (FAILED|UP-TO-DATE|SKIPPED|FROM-CACHE|NO-SOURCE))?\s*$",
                    text,
                )
            ]
            failed = [
                value["task"]
                for value in check.report["gradle_task_results"]
                if value["outcome"] == "FAILED"
            ]
            check.report["gradle_failed_tasks"] = failed
            executed = {
                value["task"].removeprefix(":")
                for value in check.report["gradle_task_results"]
                if value["outcome"] == "executed"
            }
            completed = {
                value["task"].removeprefix(":")
                for value in check.report["gradle_task_results"]
                if value["outcome"] in {"executed", "UP-TO-DATE", "FROM-CACHE"}
            }
            check.report["uncompleted_tasks"] = sorted(tasks - completed)
            required_game_tasks = tasks & {"runGameTest", "runClientGameTest"}
            if enabled and tasks & {"build", "check"}:
                required_game_tasks.add("runGameTest")
            check.report["unexecuted_game_tasks"] = sorted(required_game_tasks - executed)
            if client:
                check.report["client_scenarios"] = client_results(text, lan=lan)
            if check.report["status"] == "running":
                if failed:
                    check.report.update(status="failed", reason="gradle_task_failed")
                elif required_game_tasks - executed:
                    check.report.update(status="failed", reason="game_task_not_executed")
                elif tasks - completed:
                    check.report.update(status="failed", reason="requested_task_not_completed")
                elif client and check.report["client_scenarios"]["status"] != "passed":
                    check.report.update(status="failed", reason="client_scenario_incomplete")
        check.save()
    if stage["status"] != "passed":
        check.reject("required_evidence_missing", "failed")


def main() -> int:
    if sys.argv[1:] == ["--_hc-bootstrap"]:
        print(json.dumps({"executable": str(prepare())}))
        return 0
    return entry(ROOT, "fabric", sys.argv[1:], 1800, ["bootstrap", "gradle", "evidence"], validate)


if __name__ == "__main__":
    raise SystemExit(main())
