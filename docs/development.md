# Development

[简体中文](zh-CN/development.md) · [Documentation](README.md)

## Toolchain

The delivered inspector uses Python 3.12+ and its standard library. Development dependencies and transitive versions are pinned in [requirements-dev.txt](../requirements-dev.txt). The PEP 517 backend is setuptools 82.0.1. The bridge core uses JDK 21 and C++20 with CMake 3.25+. The separately configured Fabric toolchain and explicit game-test opt-ins are documented in [Local guest validation](guest-validation.md).

```console
python -m venv .venv
```

Activate the environment using the shell's standard activation command, then:

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
```

`verify.py` fails on an unavailable check or failed command. `package.py` builds source and wheel archives, validates the wheel inventory and installs the wheel into a fresh temporary target for subprocess checks. It does not publish a package or install anything into a game directory.

## Individual checks

```console
python -m ruff check .
python -m ruff format --check --diff .
python tools/check_repository.py
python tools/run_tests.py
```

Apply formatting with `python -m ruff format .`. Use `python -m ruff check --fix .` only for changes whose semantic diff has been reviewed. `.editorconfig` defines UTF-8, LF, final newline and indentation; `.gitattributes` normalizes source text. Keep files free of trailing whitespace.

## Bridge and Fabric validation

```console
python tools/test_bridge.py
python tools/gradle.py build
```

The first command needs installed Java/C++ tools and uses a controlled world, with no game dependencies. The second resolves the pinned Fabric toolchain, compiles all source sets and validates the production JAR; game execution is disabled by default. [Local guest validation](guest-validation.md) separates server/client GameTests, explicit EULA acceptance, Windows peer paths, screenshots and cache recovery. CI does not run those game commands.

## Source package and runtime wheel

Source code is in `src/harrancraft_tools`. The public executable is `hc-inspect`; `python -m harrancraft_tools` uses the same implementation. `__version__` in the package is the version authority; build metadata reads it without a separate version copy.

Generated PE inputs live only in temporary test directories. Runtime wheels contain package code, language resources and license metadata, not tests, executable fixtures, private input files or build caches. The sdist includes source tests, documentation, the CMake/native contracts and Fabric source sets. `guest/build`, `.gradle` caches and generated game directories are excluded; the guest is built separately from the inspector wheel.

## CI and review

CI uses fixed action commits, a read-only repository token and disabled checkout credential persistence. Its triggers are pull requests and manual runs; there is no scheduled scan, game installation or release job. Ubuntu/Python 3.12 and Windows/Python 3.13 exercise the supported tool platforms. Jobs record exact Python and source versions in their logs.

Dependencies and workflow changes require a source/compatibility review and the affected tests. Required-review enforcement, private security reporting, repository metadata and release publication are separate host settings/actions; files alone do not enable them.

The public contributor contract is self-contained in [AGENTS.md](../AGENTS.md) and [CONTRIBUTING.md](../CONTRIBUTING.md). Use PR evidence for actual results rather than parallel progress logs. [Testing](testing.md) defines what each test level can establish.
