"""Bounded execution of an explicitly owned validation process tree."""

from __future__ import annotations

import ctypes
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

POLL_SECONDS = 0.02
CLEANUP_SECONDS = 5.0
LOG_LIMIT = 32 * 1024 * 1024


class WindowsJob:
    def __init__(self) -> None:
        from ctypes import wintypes as w

        class Basic(ctypes.Structure):
            _fields_ = [
                ("process_time", ctypes.c_int64),
                ("job_time", ctypes.c_int64),
                ("flags", w.DWORD),
                ("minimum", ctypes.c_size_t),
                ("maximum", ctypes.c_size_t),
                ("processes", w.DWORD),
                ("affinity", ctypes.c_size_t),
                ("priority", w.DWORD),
                ("scheduling", w.DWORD),
            ]

        class Extended(ctypes.Structure):
            _fields_ = [
                ("basic", Basic),
                ("io", ctypes.c_uint64 * 6),
                ("memory", ctypes.c_size_t * 4),
            ]

        class Accounting(ctypes.Structure):
            _fields_ = [
                ("times", ctypes.c_int64 * 4),
                ("faults", w.DWORD),
                ("total", w.DWORD),
                ("active", w.DWORD),
                ("terminated", w.DWORD),
            ]

        self.accounting = Accounting
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, w.LPCWSTR], w.HANDLE),
            "SetInformationJobObject": ([w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD], w.BOOL),
            "QueryInformationJobObject": (
                [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.c_void_p],
                w.BOOL,
            ),
            "OpenProcess": ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            "AssignProcessToJobObject": ([w.HANDLE, w.HANDLE], w.BOOL),
            "TerminateJobObject": ([w.HANDLE, w.UINT], w.BOOL),
            "CloseHandle": ([w.HANDLE], w.BOOL),
            "QueryFullProcessImageNameW": (
                [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)],
                w.BOOL,
            ),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = arguments, result
        self.handle = self.api.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = Extended()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, without breakaway.
        try:
            self.checked(
                self.api.SetInformationJobObject(
                    self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)
                )
            )
        except BaseException:
            self.close()
            raise

    @staticmethod
    def checked(result: int) -> None:
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())

    def attach(self, pid: int) -> None:
        process = self.api.OpenProcess(0x0100 | 0x0001, False, pid)
        self.checked(process)
        try:
            self.checked(self.api.AssignProcessToJobObject(self.handle, process))
        finally:
            self.checked(self.api.CloseHandle(process))

    def active(self) -> int:
        result = self.accounting()
        self.checked(
            self.api.QueryInformationJobObject(
                self.handle, 1, ctypes.byref(result), ctypes.sizeof(result), None
            )
        )
        return result.active

    def active_process_names(self) -> list[str]:
        from ctypes import wintypes as w

        class ProcessIds(ctypes.Structure):
            _fields_ = [
                ("assigned", w.DWORD),
                ("count", w.DWORD),
                ("values", ctypes.c_size_t * 256),
            ]

        members = ProcessIds()
        self.checked(
            self.api.QueryInformationJobObject(
                self.handle, 3, ctypes.byref(members), ctypes.sizeof(members), None
            )
        )
        names = []
        for index in range(min(members.count, 256)):
            process = self.api.OpenProcess(0x1000, False, members.values[index])
            if not process:
                names.append("unavailable")
                continue
            try:
                buffer = ctypes.create_unicode_buffer(32768)
                length = w.DWORD(len(buffer))
                if self.api.QueryFullProcessImageNameW(process, 0, buffer, ctypes.byref(length)):
                    names.append(buffer.value.rsplit("\\", 1)[-1])
                else:
                    names.append("unavailable")
            finally:
                self.api.CloseHandle(process)
        return sorted(names)

    def terminate(self) -> None:
        self.checked(self.api.TerminateJobObject(self.handle, 1))

    def close(self) -> None:
        if self.handle:
            handle, self.handle = self.handle, None
            self.checked(self.api.CloseHandle(handle))


def write_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, sort_keys=True, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def owned_children() -> list[tuple[int, str]]:
    result = []
    owner = os.getpid()
    group = os.getpgrp()
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit() or int(entry.name) == owner:
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
        if int(fields[1]) == owner or int(fields[2]) == group:
            result.append((int(entry.name), fields[19]))
    return result


def _supervise(spec_path: Path) -> int:
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    metadata = Path(spec["metadata"])
    stopping = False
    deadline = time.monotonic() + spec["timeout"]
    parent = os.getppid()

    def stop(signum: int, frame: object) -> None:
        nonlocal stopping
        stopping = True

    if sys.platform == "linux":
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER, only this isolated helper.
            raise OSError(ctypes.get_errno(), "subreaper_unavailable")
        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
    # The parent establishes ownership before releasing this gate.
    if sys.stdin.buffer.readline(16) != b"start\n":
        return 125
    target = None
    result = {"exit_code": None, "cleanup": "pending", "descendants_terminated": False}
    try:
        target = subprocess.Popen(spec["command"], stdin=subprocess.DEVNULL)
        while target.poll() is None and not stopping:
            stopping = time.monotonic() >= deadline or os.getppid() != parent
            time.sleep(POLL_SECONDS)
        if stopping and target.poll() is None:
            target.terminate()
            try:
                target.wait(timeout=1)
            except subprocess.TimeoutExpired:
                target.kill()
        result["exit_code"] = target.wait(timeout=2)
        if sys.platform == "linux":
            # Orphaned descendants are adopted by this helper, never by the CLI process.
            deadline = time.monotonic() + 1
            while True:
                try:
                    pid, _ = os.waitpid(-1, os.WNOHANG)
                    if pid:
                        continue
                except ChildProcessError:
                    break
                if time.monotonic() >= deadline or stopping:
                    result["descendants_terminated"] = True
                    for child, started in owned_children():
                        try:
                            fields = (
                                Path(f"/proc/{child}/stat").read_text().rsplit(")", 1)[1].split()
                            )
                            if fields[19] == started:
                                os.kill(child, signal.SIGKILL)
                        except (ProcessLookupError, FileNotFoundError):
                            pass
                    if time.monotonic() >= deadline + 2:
                        raise TimeoutError("descendant_cleanup_timeout")
                time.sleep(POLL_SECONDS)
        result["cleanup"] = "passed"
    except Exception as error:
        import traceback

        traceback.print_exc()
        result["error_type"] = type(error).__name__
        result["cleanup"] = "failed"
        if target is not None and target.poll() is None:
            target.kill()
            target.wait(timeout=2)
    finally:
        write_json(metadata, result)
    return 0


def execute(
    command: list[str],
    cwd: Path,
    directory: Path,
    name: str,
    timeout: float,
    *,
    env: dict[str, str] | None = None,
) -> dict:
    if not command or not 0 < timeout <= 86400:
        raise ValueError("invalid_command_or_timeout")
    if not name.replace("-", "").isalnum():
        raise ValueError("invalid_stage_name")
    if os.name != "nt" and sys.platform != "linux":
        return {"status": "blocked", "reason": "unsupported_process_platform", "cleanup": "not_run"}
    directory.mkdir(parents=True, exist_ok=True)
    spec_path = directory / f"{name}.command.json"
    metadata = directory / f"{name}.process.json"
    stdout_path = directory / f"{name}.stdout.log"
    stderr_path = directory / f"{name}.stderr.log"
    for path in (spec_path, metadata, stdout_path, stderr_path):
        if path.exists() or path.is_symlink():
            raise ValueError("stage_output_exists")
    spec_path.write_text(
        json.dumps({"command": command, "metadata": str(metadata.resolve()), "timeout": timeout}),
        encoding="utf-8",
    )
    spec_path.chmod(0o600)
    start = time.monotonic()
    process = None
    job = None
    outcome = {
        "status": "running",
        "exit_code": None,
        "cleanup": "pending",
        "stdout": stdout_path.name,
        "stderr": stderr_path.name,
    }
    try:
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            if os.name == "nt":
                job = WindowsJob()
            process = subprocess.Popen(
                [sys.executable, "-S", str(Path(__file__).resolve()), str(spec_path.resolve())],
                stdin=subprocess.PIPE,
                stdout=stdout,
                stderr=stderr,
                cwd=cwd,
                env=env,
                start_new_session=os.name != "nt",
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
            )
            if job is not None:
                job.attach(process.pid)
            process.stdin.write(b"start\n")
            process.stdin.close()
            while process.poll() is None:
                if time.monotonic() - start >= timeout:
                    outcome.update(status="timed_out", reason="deadline_exceeded")
                    break
                if stdout_path.stat().st_size + stderr_path.stat().st_size > LOG_LIMIT:
                    outcome.update(status="failed", reason="log_limit_exceeded")
                    break
                time.sleep(POLL_SECONDS)
    except KeyboardInterrupt:
        outcome.update(status="interrupted", reason="interrupted")
    except Exception as error:
        outcome.update(
            status="failed",
            reason="process_start_or_control_failed",
            error_type=type(error).__name__,
        )
    finally:
        try:
            if process is not None:
                if process.stdin and not process.stdin.closed:
                    process.stdin.close()
                if process.poll() is None:
                    if job is not None:
                        job.terminate()
                    else:
                        os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=CLEANUP_SECONDS)
                    except subprocess.TimeoutExpired:
                        if job is not None:
                            job.terminate()
                        else:
                            os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=2)
                if metadata.is_file():
                    details = json.loads(metadata.read_text(encoding="utf-8"))
                    outcome.update(details)
                elif outcome["status"] == "running":
                    outcome.update(status="failed", reason="supervisor_result_missing")
                if job is not None:
                    deadline = time.monotonic() + 1
                    while job.active() and time.monotonic() < deadline:
                        time.sleep(POLL_SECONDS)
                    if job.active() and outcome["status"] == "running":
                        try:
                            names = job.active_process_names()
                        except OSError:
                            names = []
                        if names and all(name.casefold() == "vctip.exe" for name in names):
                            # The MSVC telemetry helper must exit naturally; it is not exempt.
                            grace = min(start + timeout, time.monotonic() + 30)
                            while job.active() and time.monotonic() < grace:
                                time.sleep(POLL_SECONDS)
                    if job.active():
                        outcome["descendants_terminated"] = True
                        outcome.setdefault("reason", "owned_descendant_retained")
                        try:
                            outcome["remaining_processes"] = job.active_process_names()
                        except OSError:
                            outcome["remaining_processes"] = ["unavailable"]
                        job.terminate()
                    deadline = time.monotonic() + CLEANUP_SECONDS
                    while job.active() and time.monotonic() < deadline:
                        time.sleep(POLL_SECONDS)
                    outcome["cleanup"] = "passed" if job.active() == 0 else "failed"
                else:
                    try:
                        os.killpg(process.pid, 0)
                    except ProcessLookupError:
                        outcome["cleanup"] = "passed"
                    else:
                        # The supervisor can fail before reaping its children; still revoke its group.
                        os.killpg(process.pid, signal.SIGKILL)
                        outcome["cleanup"] = "failed"
                if outcome["status"] == "running":
                    passed = (
                        outcome["exit_code"] == 0
                        and outcome["cleanup"] == "passed"
                        and not outcome.get("descendants_terminated")
                        and not outcome.get("error_type")
                    )
                    outcome["status"] = "passed" if passed else "failed"
                if outcome["cleanup"] != "passed" and outcome["status"] == "passed":
                    outcome["status"] = "failed"
            else:
                outcome["cleanup"] = "not_run"
        except Exception as error:
            outcome.update(cleanup="failed", cleanup_error_type=type(error).__name__)
            if outcome["status"] in {"running", "passed"}:
                outcome["status"] = "failed"
        finally:
            if job is not None:
                try:
                    job.close()
                except OSError as error:
                    outcome.update(
                        status="failed", cleanup="failed", cleanup_error_type=type(error).__name__
                    )
            # Raw commands can contain local paths; they are not part of the retained evidence.
            spec_path.unlink(missing_ok=True)
    if stdout_path.exists() and stderr_path.exists():
        if stdout_path.stat().st_size + stderr_path.stat().st_size > LOG_LIMIT:
            outcome.update(status="failed", reason="log_limit_exceeded")
    outcome["elapsed_seconds"] = round(time.monotonic() - start, 6)
    write_json(metadata, outcome)
    return outcome


if __name__ == "__main__":
    raise SystemExit(_supervise(Path(sys.argv[1])))
