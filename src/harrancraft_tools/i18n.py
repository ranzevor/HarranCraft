from __future__ import annotations

import json
import os
from functools import lru_cache
from importlib.resources import files


def select_language(value: str | None = None) -> str:
    candidate = value
    if candidate is None:
        candidate = next((os.environ[k] for k in ("LC_ALL", "LC_MESSAGES", "LANG") if os.environ.get(k)), "en-US")
    candidate = candidate.replace("_", "-").split(".", 1)[0].lower()
    return "zh-CN" if candidate in ("zh", "zh-cn", "zh-sg", "zh-hans") else "en-US"


@lru_cache(maxsize=2)
def catalog(language: str) -> dict[str, str]:
    name = select_language(language)
    return json.loads(files("harrancraft_tools").joinpath("locales", f"{name}.json").read_text(encoding="utf-8"))


def text(key: str, language: str, **values: object) -> str:
    source = catalog(language).get(key)
    if source is None:
        source = catalog("en-US")[key]
    return source.format(**values)
