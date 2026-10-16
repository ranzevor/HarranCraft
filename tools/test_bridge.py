"""Compile and compare the Java/C++ wire endpoints without a game runtime."""

from __future__ import annotations

import hashlib
import os
import re
import sys
from pathlib import Path

if __package__:
    from .validation import Validation, entry
else:
    from validation import Validation, entry

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "bridge"
PACKAGE = "io.ranzevor.harrancraft.bridge"
STAGES = [
    "configure",
    "native-build",
    "native-tests",
    *(
        [
            "host-image-probe",
            "host-image-comparison",
            "host-module-probe",
            "host-module-comparison",
        ]
        if os.name == "nt"
        else []
    ),
    "java-build",
    "core-tests",
    "lifecycle-tests",
    "native-frame",
    "java-frame",
    "frame-comparison",
    "process-tests",
    "pipe-tests",
    "observation-tests",
]


def validate(check: Validation, arguments: list[str]) -> None:
    if arguments:
        check.reject("unexpected_bridge_argument")
    check.preflight(("cmake", "ctest", "java", "javac"))
    if check.settings["preflight"]:
        return
    check.acquire()
    native, java = BUILD / "native", BUILD / "java" / check.directory.name
    for directory in (native, java):
        if directory.is_symlink() or not directory.resolve().is_relative_to(ROOT):
            check.reject("build_directory_link_rejected")
    java.mkdir(parents=True, exist_ok=True)
    check.run("configure", ["cmake", "-S", ".", "-B", str(native), "-DCMAKE_BUILD_TYPE=Debug"])
    check.run(
        "native-build", ["cmake", "--build", str(native), "--config", "Debug", "--parallel", "2"]
    )
    check.run(
        "native-tests", ["ctest", "--test-dir", str(native), "-C", "Debug", "--output-on-failure"]
    )
    bin_dir = native / "Debug" if (native / "Debug").is_dir() else native
    if os.name == "nt":
        test_binary = bin_dir / "hc-host-probe-test.exe"
        actual = check.run("host-image-probe", [str(test_binary), "--sha256"]).strip()
        if test_binary.stat().st_size > 512 * 1024 * 1024:
            check.reject("host_probe_test_binary_oversize", "failed")
        with test_binary.open("rb") as stream:
            expected = hashlib.file_digest(stream, "sha256").hexdigest()
        comparison = next(
            item for item in check.report["stages"] if item["name"] == "host-image-comparison"
        )
        comparison["status"] = (
            "passed" if re.fullmatch(r"[0-9a-f]{64}", actual) and actual == expected else "failed"
        )
        check.save()
        if comparison["status"] != "passed":
            check.reject("host_image_digest_mismatch", "failed")
        module_binary = bin_dir / "hc-host-probe.dll"
        module_result = check.run(
            "host-module-probe", [str(test_binary), "--module-sha256"]
        ).strip()
        if module_binary.stat().st_size > 512 * 1024 * 1024:
            check.reject("host_module_test_binary_oversize", "failed")
        with module_binary.open("rb") as stream:
            module_expected = hashlib.file_digest(stream, "sha256").hexdigest()
        module_comparison = next(
            item for item in check.report["stages"] if item["name"] == "host-module-comparison"
        )
        module_comparison["status"] = (
            "passed"
            if re.fullmatch(r"[0-9a-f]{64}", module_result) and module_result == module_expected
            else "failed"
        )
        check.save()
        if module_comparison["status"] != "passed":
            check.reject("host_module_digest_mismatch", "failed")
    bridge = Path("io/ranzevor/harrancraft/bridge")
    sources = [
        str(path)
        for directory in ("main", "test")
        for path in sorted((ROOT / "guest/src" / directory / "java" / bridge).glob("*.java"))
    ]
    if not sources:
        check.reject("bridge_sources_missing")
    check.run(
        "java-build",
        ["javac", "--release", "21", "-Xlint:all", "-Werror", "-d", str(java), *sources],
    )
    check.run("core-tests", ["java", "-cp", str(java), f"{PACKAGE}.CoreTests"])
    check.run("lifecycle-tests", ["java", "-cp", str(java), f"{PACKAGE}.LifecycleTests"])
    bin_dir = native / "Debug" if (native / "Debug").is_dir() else native
    extension = ".exe" if os.name == "nt" else ""
    peer = bin_dir / f"hc-bridge-peer{extension}"
    golden = check.run("native-frame", [str(bin_dir / f"hc-wire-test{extension}"), "--golden"])
    java_golden = check.run(
        "java-frame", ["java", "-cp", str(java), f"{PACKAGE}.CoreTests", "--golden"]
    )
    comparison = next(
        stage for stage in check.report["stages"] if stage["name"] == "frame-comparison"
    )
    comparison["status"] = (
        "passed"
        if re.fullmatch(r"[0-9a-f]{1024}", golden.strip()) and golden.strip() == java_golden.strip()
        else "failed"
    )
    check.save()
    if comparison["status"] == "failed":
        check.reject("wire_frame_mismatch", "failed")
    check.run("process-tests", ["java", "-cp", str(java), f"{PACKAGE}.ProcessTests", str(peer)])
    check.run(
        "pipe-tests",
        [
            "java",
            "-cp",
            str(java),
            f"{PACKAGE}.PipeTests",
            str(bin_dir / f"hc-pipe-peer{extension}"),
        ],
    )

    check.run(
        "observation-tests", ["java", "-cp", str(java), f"{PACKAGE}.ObservationTests", str(peer)]
    )


def main() -> int:
    return entry(ROOT, "bridge", sys.argv[1:], 600, STAGES, validate)


if __name__ == "__main__":
    raise SystemExit(main())
