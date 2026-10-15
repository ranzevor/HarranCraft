# Initial tooling evidence

Date: 2026-10-15. Scope: the inspector and repository tooling, not Dying Light integration.

## Local execution

| Check | Environment / result |
| --- | --- |
| Unit, CLI and repository-contract tests | Linux x86_64, CPython 3.13.5; 37 test methods passed, including 512 truncated-header subcases |
| Streaming hash memory | A generated file with 32 MiB padding; exact hash matched and traced peak allocation stayed below 6 MiB |
| Source/wheel build and installed CLI | setuptools 82.0.1; sdist and wheel built; isolated `python -S` subprocesses verified help, version, successful inspection and error behavior |
| Independent native PE comparison | Clang/LLD 17.0.0 generated an original 2,048-byte AMD64 DLL; GNU objdump 2.44 matched export ordinals, RVAs, names and forwarding |

The original native probe exported `hc_probe_identity` and a `hc_forward` forwarder to `KERNEL32.Sleep`. Linking used `/dll /noentry /nodefaultlib /machine:x64 /timestamp:0`. The inspected image SHA-256 was `98a87f317c6993be4480c364a53153433c58adf5b25bd07866a779bb8414d479`. The probe was parsed, not executed. Its binaries are not repository or runtime-package content.

The local environment could not resolve PyPI for additional development-tool installation. Existing local tools ran the checks listed above; Ruff and the exact pinned dependency environment are separately checked by CI. This does not turn an unavailable local check into a pass.

## CI acceptance

The repository workflow runs source/format/catalog/schema checks, tests and packaging on Ubuntu/Python 3.12 and Windows/Python 3.13. The Windows job additionally inspects the active CPython DLL and compares its fixed file version with the Windows version API, checking for a known Python export.

Exact CI source heads, job outcomes and logs belong to the initialization PR and its linked run, rather than being inferred from this description. The POSIX-only FIFO test is not applicable on Windows; its skip is explicit. No game, graphics-device, native-collider, mode-transition or gameplay test is represented by this evidence.
