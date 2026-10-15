# Binary inspector contract

[简体中文](zh-CN/inspector.md) · [Documentation](README.md)

## Input and operation

`hc-inspect [--language LANGUAGE] [--json] [--exports] FILE ...` reads 1–16 explicit regular files. The supported inspection format is AMD64 (`0x8664`) PE32+ (`0x20b`) with 1–96 non-overlapping sections. Both EXE and DLL images are accepted. No image is mapped as executable, loaded, installed or sent to a service.

A file is opened read-only. Relative addresses are translated to file-backed regions; virtual zero-fill is not treated as metadata. Header/table bounds, export ordinals, strings, resource traversal and integer ranges are checked. SHA-256 is streamed over the entire opened file. Before/after file metadata and the observed length must agree; a detected concurrent change fails the inspection. Use an unchanged input for reproducible results. This is not a hostile-writer snapshot protocol or a complete Windows-loader validator.

Limits are technical parser bounds: 512 MiB per file; 8 MiB total metadata reads; 65,536 export address slots and name pointers each; 4,096 bytes per zero-terminated export string; 4,096 visited version-resource directory entries; three directory levels. Counts are validated before traversal/allocation. Hashing uses 1 MiB chunks.

## Output

CLI output is UTF-8. The default text report prints the input position, image type, size, SHA-256, export count and any fixed file versions. `--json` emits [inspection-v1.schema.json](../schemas/inspection-v1.schema.json). `--exports` includes every nonzero export slot, its ordinal, all names attached to it, and a forwarder string when applicable. An ordinal-only export has an empty `names` array.

Reports intentionally omit input paths, filenames, imported library code and raw resource data. `input_index` is 1-based and preserves command-line order. Inspectors must not interpret an export name as a validated calling convention or game interface.

| Field | Meaning |
| --- | --- |
| `schema_version` | Report layout version; currently 1 |
| `tool_version` | Inspector package version |
| `results[].status` | `inspected` or `error`; not a game compatibility status |
| `image.sha256` / `file_size` | Identity and size of the opened file |
| `machine`, `format`, `kind` | AMD64, PE32+, EXE/DLL classification |
| `coff_timestamp` | Raw COFF field; not a reliable product version or release date |
| `file_versions` | Distinct fixed `VS_FIXEDFILEINFO` file versions; empty when absent |
| `sections` | File offsets, RVAs, sizes and characteristics of the inspected section table |
| `export_count`, `named_export_count` | Nonzero export slots and attached names, respectively |
| `exports` | Optional detailed export table, including aliases and forwarding |

Do not use a filename, embedded version string or matching export list as a compatibility certificate. A future host adapter must separately validate accepted build fingerprints, its runtime mode and invocation semantics. Current output contains no `compatible` or supported-game claim.

## Errors and exit status

| Exit | Meaning |
| --- | --- |
| 0 | Every requested input was inspected, or help/version was printed |
| 1 | One or more input inspections failed; successful results remain in input order |
| 2 | Invalid command arguments; a usage message is written to stderr |
| 130 | Interrupted before completion |

For normal inspection failures, JSON mode prints one complete report on stdout, including stable error codes and localized messages. Invalid arguments and interruption do not emit a partial JSON report. A consumer must check the exit code and each result.

Error codes distinguish invalid headers/layout, unsupported machine/format, section overlap, unbacked RVAs, invalid exports/resources, resource limits, changed inputs, non-regular files and unreadable inputs. Internal exception traces and personal paths are not normal error messages.

## Language and evolution

`--language` overrides `LC_ALL`, `LC_MESSAGES`, then `LANG`. `zh`, `zh-CN`, `zh-SG` and `zh-Hans` select Simplified Chinese; other values select en-US. Locale underscores and encoding suffixes are normalized. Missing translated keys fall back to English; build-time checks require matching keys and placeholders.

Machine keys and error codes are language-neutral. Incompatible report changes require a new schema version and tests for affected consumers. A report version is distinct from any future game bridge ABI.

Format sources: [PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), [VS_VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo), [VS_FIXEDFILEINFO](https://learn.microsoft.com/en-us/windows/win32/api/verrsrc/ns-verrsrc-vs_fixedfileinfo).
