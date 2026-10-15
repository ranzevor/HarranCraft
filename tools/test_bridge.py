"""Compile and compare the Java/C++ wire endpoints without a game runtime."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "bridge"
PACKAGE = "io.ranzevor.harrancraft.bridge"


def run(*command: str, capture: bool = False) -> str:
    result = subprocess.run(
        list(command), cwd=ROOT, check=True, timeout=120, capture_output=capture, text=True
    )
    return result.stdout if capture else ""


def main() -> None:
    native = BUILD / "native"
    java = BUILD / "java"
    java.mkdir(parents=True, exist_ok=True)
    run("cmake", "-S", ".", "-B", str(native), "-DCMAKE_BUILD_TYPE=Debug")
    run("cmake", "--build", str(native), "--config", "Debug", "--parallel", "2")
    run("ctest", "--test-dir", str(native), "-C", "Debug", "--output-on-failure")
    bridge = Path("io/ranzevor/harrancraft/bridge")
    sources = [
        str(path)
        for directory in ("main", "test")
        for path in sorted((ROOT / "guest/src" / directory / "java" / bridge).glob("*.java"))
    ]
    run("javac", "--release", "21", "-Xlint:all", "-Werror", "-d", str(java), *sources)
    run("java", "-cp", str(java), f"{PACKAGE}.CoreTests")
    run("java", "-cp", str(java), f"{PACKAGE}.LifecycleTests")
    bin_dir = native / "Debug" if (native / "Debug").is_dir() else native
    extension = ".exe" if os.name == "nt" else ""
    peer = bin_dir / f"hc-bridge-peer{extension}"
    golden = run(str(bin_dir / f"hc-wire-test{extension}"), "--golden", capture=True)
    java_golden = run("java", "-cp", str(java), f"{PACKAGE}.CoreTests", "--golden", capture=True)
    if golden.strip() != java_golden.strip():
        raise ValueError("Java/C++ golden frames differ")
    run("java", "-cp", str(java), f"{PACKAGE}.ProcessTests", str(peer))
    run("java", "-cp", str(java), f"{PACKAGE}.PipeTests", str(bin_dir / f"hc-pipe-peer{extension}"))
    print("bridge-contract: passed (controlled world and native pipe faults)")


if __name__ == "__main__":
    main()
