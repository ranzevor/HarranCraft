# Contributing to HarranCraft

[简体中文](docs/zh-CN/contributing.md)

## Choose a contribution

Use the issue forms for reproducible defects, specific feature proposals and host experiments. Search existing issues first. For changes to gameplay authority, protocol layouts, dependencies or supported platforms, explain the affected contract before expanding the implementation. Small focused fixes can go directly to a PR.

English and Simplified Chinese are accepted. Follow the [code of conduct](CODE_OF_CONDUCT.md). Report exploitable defects through [SECURITY.md](SECURITY.md), not a public reproduction.

## Prepare and validate

Use Python 3.12+ and install the pinned development requirements in an isolated environment:

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
```

See the [development guide](docs/development.md) for the individual checks and formatting commands. Generated files belong in ignored output directories. Game copies are unnecessary for the current test suite; do not upload them to an issue or CI artifact.

## Changes and review

Create a focused branch from current main. Use commit subjects in the form `type: imperative summary`, such as `fix: reject overlapping PE sections`. Applicable types are `feat`, `fix`, `docs`, `test`, `refactor`, `build` and `chore`. Describe the actual change; avoid task transcripts or unverified claims.

A PR must identify its purpose, behavior/contract changes, test commands and results, compatibility impact, and any unrun validation. Code, tests, resources and documentation for the same behavior belong together. Follow [AGENTS.md](AGENTS.md); an automated check cannot replace semantic review.

The repository owner, `@ranzevor`, holds project acceptance and merge responsibility. `CODEOWNERS` records review ownership; branch protection and required checks are separate GitHub settings. A merge is not a game release or approval to modify a user's installation.

## Source and licensing

Submit code and documentation you have the right to contribute under the repository's MIT License, retaining third-party license and attribution obligations. Include a component's exact source, version and license when introducing it. Research references are not automatically implementation dependencies. Do not commit game assets, binaries, proprietary SDKs, saves, secrets or personal environment paths.

## Experimental results

Attach an evidence summary with the revision, target build identity, test level, controlled inputs, expected/observed behavior and cleanup result. Separate native player/enemy/projectile collision from navigation. Separate nonlethal damage from forced death. A synthetic fixture or successful build must not be labeled as a real game test.
