from __future__ import annotations

import ast
import json
import re
import sys
import tomllib
from pathlib import Path
from string import Formatter
from urllib.parse import unquote, urlsplit

import jsonschema
import yaml

ROOT = Path(__file__).resolve().parents[1]
IGNORED = {".git", ".venv", "venv", "build", "dist", "out", "reports", "__pycache__", ".ruff_cache"}
REQUIRED = {
    "README.md",
    "README.zh-CN.md",
    "LICENSE",
    "AGENTS.md",
    "CONTRIBUTING.md",
    "CODE_OF_CONDUCT.md",
    "SECURITY.md",
    "SUPPORT.md",
    "CHANGELOG.md",
    "THIRD_PARTY_NOTICES.md",
    ".editorconfig",
    ".gitattributes",
    ".gitignore",
    "pyproject.toml",
    "requirements-dev.txt",
    "MANIFEST.in",
    ".github/CODEOWNERS",
    ".github/pull_request_template.md",
    ".github/ISSUE_TEMPLATE/config.yml",
    ".github/ISSUE_TEMPLATE/bug_report.yml",
    ".github/ISSUE_TEMPLATE/feature_request.yml",
    ".github/ISSUE_TEMPLATE/experiment.yml",
    ".github/workflows/ci.yml",
    "schemas/inspection-v1.schema.json",
    "docs/README.md",
    "docs/zh-CN/README.md",
}
TEXT_SUFFIXES = {".py", ".md", ".json", ".toml", ".yml", ".yaml", ".txt", ".in", ".typed"}
BINARY_SUFFIXES = {
    ".exe",
    ".dll",
    ".pyd",
    ".asi",
    ".obj",
    ".pdb",
    ".lib",
    ".exp",
    ".whl",
    ".jar",
    ".zip",
}
LINK = re.compile(r"!?\[[^\]\n]*\]\(([^)\n]+)\)")
REFERENCE = re.compile(r"(?m)^\[([^\]]+)\]:\s*(\S+)")
USE = re.compile(r"(?<!!)\[[^\]\n]+\]\[([^\]\n]+)\]")


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def prose(contents: str) -> str:
    lines = []
    fence = None
    for line in contents.splitlines():
        marker = re.match(r"\s*(`{3,}|~{3,})", line)
        if marker:
            token = marker.group(1)
            if fence is None:
                fence = token[0]
            elif fence == token[0]:
                fence = None
            continue
        if fence is None:
            lines.append(line)
    return "\n".join(lines)


def anchors(contents: str) -> set[str]:
    result = set()
    used: dict[str, int] = {}
    for heading in re.findall(r"(?m)^#{1,6}\s+(.+?)\s*#*\s*$", prose(contents)):
        heading = re.sub(r"\[([^]]+)\]\([^)]+\)", r"\1", heading)
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        count = used.get(slug, 0)
        used[slug] = count + 1
        result.add(slug if count == 0 else f"{slug}-{count}")
    return result


def check_markdown(path: Path, root: Path = ROOT) -> int:
    contents = prose(path.read_text(encoding="utf-8"))
    definitions = dict(REFERENCE.findall(contents))
    for name in USE.findall(contents):
        if name not in definitions:
            raise ValueError(f"{path.name}: undefined reference {name}")
    targets = LINK.findall(contents) + list(definitions.values())
    count = 0
    for target in targets:
        target = target.strip("<>")
        parsed = urlsplit(target)
        if parsed.scheme in {"https", "http", "mailto"}:
            continue
        if parsed.scheme or parsed.netloc:
            raise ValueError(f"{path.name}: unsupported link")
        destination = (
            (path.parent / unquote(parsed.path)).resolve() if parsed.path else path.resolve()
        )
        if not destination.is_relative_to(root.resolve()) or not destination.exists():
            raise ValueError(f"{path.name}: missing local link {target}")
        if parsed.fragment and destination.suffix == ".md":
            if unquote(parsed.fragment) not in anchors(destination.read_text(encoding="utf-8")):
                raise ValueError(f"{path.name}: missing anchor {target}")
        count += 1
    return count


def check_catalogs(root: Path = ROOT) -> int:
    base = root / "src/harrancraft_tools/locales"
    catalogs = [
        json.loads(
            (base / f"{lang}.json").read_text(encoding="utf-8"), object_pairs_hook=unique_object
        )
        for lang in ("en-US", "zh-CN")
    ]
    if catalogs[0].keys() != catalogs[1].keys():
        raise ValueError("language catalogs have different keys")
    formatter = Formatter()
    for key in catalogs[0]:
        fields = []
        for catalog in catalogs:
            value = catalog[key]
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"empty language value: {key}")
            fields.append(
                sorted(name for _, name, _, _ in formatter.parse(value) if name is not None)
            )
        if fields[0] != fields[1]:
            raise ValueError(f"placeholder mismatch: {key}")
    return len(catalogs[0])


def check_workflow(data: dict) -> None:
    if set(data["on"]) != {"pull_request", "workflow_dispatch"}:
        raise ValueError("unexpected CI trigger")
    if data.get("permissions") != {"contents": "read"}:
        raise ValueError("CI must use read-only repository permissions")
    for job in data["jobs"].values():
        if "permissions" in job:
            raise ValueError("job permission overrides require review")
        for step in job["steps"]:
            action = step.get("uses")
            if action and not re.fullmatch(r"actions/(checkout|setup-python)@[0-9a-f]{40}", action):
                raise ValueError("CI actions must be reviewed and commit-pinned")
            if action and action.startswith("actions/checkout@"):
                if step.get("with", {}).get("persist-credentials") != "false":
                    raise ValueError("checkout must not persist credentials")


def check_issue_form(data: dict) -> None:
    if not data.get("name") or not data.get("description") or not data.get("body"):
        raise ValueError("incomplete issue form")
    seen = set()
    for field in data["body"]:
        kind = field.get("type")
        if kind not in {"markdown", "input", "textarea", "dropdown", "checkboxes"}:
            raise ValueError("invalid issue form field type")
        if kind == "markdown":
            if not field.get("attributes", {}).get("value"):
                raise ValueError("empty issue form description")
            continue
        identity = field.get("id")
        if not identity or identity in seen or not field.get("attributes", {}).get("label"):
            raise ValueError("invalid issue form field")
        seen.add(identity)
        if kind in {"dropdown", "checkboxes"} and not field["attributes"].get("options"):
            raise ValueError("missing issue form choices")


def main() -> None:
    missing = sorted(p for p in REQUIRED if not (ROOT / p).is_file())
    if missing:
        raise ValueError(f"missing repository files: {missing}")
    checked = 0
    links = 0
    for path in ROOT.rglob("*"):
        parts = path.relative_to(ROOT).parts
        if any(p in IGNORED or p.endswith(".egg-info") for p in parts) or not path.is_file():
            continue
        if path.suffix in BINARY_SUFFIXES:
            raise ValueError(f"binary outside ignored output: {path.name}")
        if path.suffix not in TEXT_SUFFIXES and path.name not in {
            "LICENSE",
            "CODEOWNERS",
            ".gitignore",
            ".gitattributes",
            ".editorconfig",
        }:
            continue
        raw = path.read_bytes()
        value = raw.decode("utf-8")
        if raw.startswith(b"\xef\xbb\xbf") or b"\r" in raw or (raw and not raw.endswith(b"\n")):
            raise ValueError(f"invalid text encoding/newline: {path.name}")
        if any(line.rstrip() != line for line in value.splitlines()):
            raise ValueError(f"trailing whitespace: {path.name}")
        if path.suffix == ".py":
            ast.parse(value, filename=path.name)
        elif path.suffix == ".json":
            json.loads(value, object_pairs_hook=unique_object)
        elif path.suffix == ".toml":
            tomllib.loads(value)
        elif path.suffix in {".yml", ".yaml"}:
            data = yaml.load(value, Loader=yaml.BaseLoader)
            if ".github/workflows/" in path.as_posix():
                check_workflow(data)
            elif path.parent.name == "ISSUE_TEMPLATE" and path.name != "config.yml":
                check_issue_form(data)
        elif path.suffix == ".md":
            links += check_markdown(path)
        checked += 1
    keys = check_catalogs()
    schema = json.loads((ROOT / "schemas/inspection-v1.schema.json").read_text())
    jsonschema.Draft202012Validator.check_schema(schema)
    print(
        f"Repository checks passed: {checked} text files, {links} local links, {keys} keys per locale"
    )


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError) as error:
        print(f"Repository check failed: {error}", file=sys.stderr)
        raise SystemExit(1) from None
