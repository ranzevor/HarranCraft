from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from setuptools import build_meta

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    os.chdir(ROOT)
    sys.path.insert(0, str(ROOT / "src"))
    from harrancraft_tools import __version__

    sys.path.insert(0, str(ROOT / "tests"))
    from fixtures import image_bytes

    destination = ROOT / "dist"
    destination.mkdir(exist_ok=True)
    source_name = build_meta.build_sdist(str(destination))
    wheel_name = build_meta.build_wheel(str(destination))
    wheel = destination / wheel_name
    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()
        for name in names:
            if not (name.startswith("harrancraft_tools/") or name.startswith("harrancraft_tools-")):
                raise ValueError("unexpected wheel member")
            if Path(name).suffix in {".dll", ".exe", ".pyd", ".asi", ".obj", ".jar"}:
                raise ValueError("binary fixture included in runtime wheel")
        for suffix in (
            "locales/en-US.json",
            "locales/zh-CN.json",
            "licenses/LICENSE",
            "licenses/THIRD_PARTY_NOTICES.md",
        ):
            if not any(name.endswith(suffix) for name in names):
                raise ValueError(f"missing packaged resource: {suffix}")
    with tempfile.TemporaryDirectory() as temporary:
        target = Path(temporary) / "site"
        subprocess.run(
            [
                sys.executable,
                "-m",
                "pip",
                "install",
                "--disable-pip-version-check",
                "--no-deps",
                "--no-index",
                "--target",
                str(target),
                str(wheel),
            ],
            check=True,
            capture_output=True,
        )
        env = dict(os.environ, PYTHONPATH=str(target), PYTHONIOENCODING="utf-8")
        for args, expected in (
            (["--help", "--language", "zh-CN"], "用法"),
            (["--version"], __version__),
        ):
            result = subprocess.run(
                [sys.executable, "-S", "-m", "harrancraft_tools", *args],
                cwd=temporary,
                env=env,
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=15,
                check=True,
            )
            if expected not in result.stdout or result.stderr:
                raise ValueError("installed wheel CLI verification failed")
        sample = Path(temporary) / "probe.dll"
        sample.write_bytes(image_bytes())
        completed = subprocess.run(
            [sys.executable, "-S", "-m", "harrancraft_tools", "--json", "--exports", str(sample)],
            cwd=temporary,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=15,
            check=True,
        )
        inspected = json.loads(completed.stdout)["results"][0]
        if inspected["status"] != "inspected" or inspected["image"]["file_versions"] != [
            "1.55.0.0"
        ]:
            raise ValueError("installed wheel inspection failed")
        if inspected["image"]["sha256"] != hashlib.sha256(image_bytes()).hexdigest():
            raise ValueError("installed wheel hash mismatch")
        result = subprocess.run(
            [sys.executable, "-S", "-m", "harrancraft_tools", "--json", "absent.dll"],
            cwd=temporary,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=15,
        )
        if (
            result.returncode != 1
            or json.loads(result.stdout)["results"][0]["error"]["code"] != "input_unreadable"
        ):
            raise ValueError("installed wheel error contract failed")
    print(
        json.dumps(
            {
                "sdist": source_name,
                "wheel": wheel_name,
                "wheel_sha256": hashlib.sha256(wheel.read_bytes()).hexdigest(),
                "installed_cli": "passed",
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
