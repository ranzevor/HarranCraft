from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
commands = [
    [sys.executable, "-m", "ruff", "check", "."],
    [sys.executable, "-m", "ruff", "format", "--check", "--diff", "."],
    [sys.executable, "tools/check_repository.py"],
    [sys.executable, "tools/run_tests.py"],
]
failed = False
for command in commands:
    completed = subprocess.run(command, cwd=ROOT, check=False)
    failed = failed or completed.returncode != 0
raise SystemExit(1 if failed else 0)
