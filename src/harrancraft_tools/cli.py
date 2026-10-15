from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from .i18n import select_language, text
from .pe import InspectionError, inspect_file

MAX_INPUTS = 16


class Parser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise InspectionError("arguments")


def _parser() -> Parser:
    parser = Parser(add_help=False, allow_abbrev=False, exit_on_error=False)
    parser.add_argument("--language")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--exports", action="store_true")
    parser.add_argument("--help", "-h", action="store_true")
    parser.add_argument("--version", action="store_true")
    parser.add_argument("paths", nargs="*")
    return parser


def language_hint(arguments: list[str]) -> str:
    selected = None
    for index, value in enumerate(arguments):
        if value == "--":
            break
        if value.startswith("--language="):
            selected = value.split("=", 1)[1]
        elif value == "--language" and index + 1 < len(arguments):
            selected = arguments[index + 1]
    return select_language(selected)


def run(arguments: list[str]) -> int:
    language = language_hint(arguments)
    try:
        args = _parser().parse_args(arguments)
        language = select_language(args.language)
        if args.help:
            print(text("help", language))
            return 0
        if args.version:
            print(__version__)
            return 0
        if not 1 <= len(args.paths) <= MAX_INPUTS:
            raise InspectionError("arguments")
    except (argparse.ArgumentError, InspectionError):
        print(text("error.arguments", language), file=sys.stderr)
        return 2
    results = []
    failed = False
    for index, value in enumerate(args.paths, 1):
        try:
            image = inspect_file(Path(value), include_exports=args.exports)
            results.append({"input_index": index, "status": "inspected", "image": image})
        except InspectionError as exc:
            failed = True
            results.append(
                {
                    "input_index": index,
                    "status": "error",
                    "error": {
                        "code": exc.code,
                        "message": text(f"error.{exc.code}", language),
                    },
                }
            )
    if args.json:
        print(
            json.dumps(
                {"schema_version": 1, "tool_version": __version__, "results": results},
                ensure_ascii=True,
                indent=2,
            )
        )
    else:
        for result in results:
            index = result["input_index"]
            if result["status"] == "error":
                print(
                    text("input.error", language, index=index, message=result["error"]["message"]),
                    file=sys.stderr,
                )
                continue
            image = result["image"]
            print(
                text(
                    "input.summary",
                    language,
                    index=index,
                    size=image["file_size"],
                    kind=image["kind"],
                    sha256=image["sha256"],
                    exports=image["export_count"],
                )
            )
            for version in image["file_versions"]:
                print(text("input.version", language, version=version))
            for export in image.get("exports", []):
                print(json.dumps(export, ensure_ascii=True, sort_keys=True))
    return 1 if failed else 0


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    try:
        return run(sys.argv[1:])
    except KeyboardInterrupt:
        print(text("error.interrupted", language_hint(sys.argv[1:])), file=sys.stderr)
        return 130
    except BrokenPipeError:
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
