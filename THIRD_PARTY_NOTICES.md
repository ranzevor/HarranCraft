# Third-party components and sources

The runtime wheel contains HarranCraft code and language resources. It uses Python's standard library and does not vendor an external runtime library.

Development tools are installed separately through [requirements-dev.txt](requirements-dev.txt): setuptools (MIT), Ruff (MIT), PyYAML (MIT), jsonschema, attrs, referencing, jsonschema-specifications and rpds-py (MIT), typing_extensions (PSF-2.0), and packaging (Apache-2.0 OR BSD-2-Clause). Their distributions retain their own license notices. The pinned GitHub checkout and setup-python actions are CI tools, not runtime components.

[Microsoft PE/COFF documentation](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) and [version-resource documentation](https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo) define the inspected formats. The parser is an independent implementation; game data is neither bundled nor required by its generated-input tests.

[GModLight](https://github.com/GooseMcGee/DyingLight-GMod) is a host-interface research reference under its own MIT license. No GModLight, MinHook, ASI-loader, GMod SDK or guest code is included in this version. Any later source reuse must retain the component's license and copyright and record its fixed revision here.

Minecraft and Dying Light game files, trademarks and other assets remain subject to their respective holders' rights. The repository MIT License does not grant rights to those materials.
