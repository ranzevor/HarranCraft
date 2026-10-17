# Verification and evidence

[Documentation](README.md)

## Current checks

`python tools/verify.py` runs repository validation, Ruff lint and formatting checks, and the test suite. Missing tooling and failed commands fail the entry point. `python tools/package.py` builds an sdist and wheel, inspects package contents and exercises the installed wheel from outside the source checkout.

Generated PE inputs cover EXE/DLL headers, names/ordinals/forwarders/aliases, file versions, truncated data, oversized counts, overlapping sections, virtual zero-fill, corrupt resource trees, file changes, partial CLI failures and language fallback. A large generated file checks that hashing memory does not scale with file size. The POSIX FIFO case is explicitly inapplicable on Windows, not silently passed there.

Schema validation uses the declared JSON Schema dialect and accepts both success and failure entries. CLI tests use real subprocesses and verify exit codes, output streams and omission of file paths. Packaging tests prevent the runtime wheel from carrying tests, binaries or private inputs.

## Host observation checks

The `host-build` CTest covers complete EXE/DLL build matching, case-insensitive name normalization, malformed/duplicate/missing module rejection, bounded profiles, owner-thread access, revoked sessions and stale frame epochs. The Windows host-probe process additionally passes its real measured EXE and loaded probe-DLL fingerprints into this gate and verifies changed-DLL rejection. These checks do not establish a supported Dying Light build.

Windows native bridge tests additionally load the read-only host probe DLL in a controlled executable, validate its fixed C ABI and repeated-call resource retirement, and compare the returned executable SHA-256 to Python's streaming `hashlib.file_digest` result. This checks Windows process-level on-disk fingerprinting, not the Dying Light memory image or engine mode.


The native `host-observation` CTest exercises the read-only [H1 observation core](host-observation.md) under a synthetic build allowlist and generated camera data. It verifies rejected identity, mode, read phase and stale frame conditions, strict source-thread access, coordinate transformation and session revocation. It is not an actual host callback or depth-buffer test.

The Fabric `MinecraftCollisionShapes` source collects Minecraft's actual block collision geometry for subscribed, loaded positions under a matching guest epoch. Its GameTests cover full-block, slab, air and an authorized client-world snapshot, including refusal after disconnect. CI compiles these GameTest classes but does not execute Minecraft, establish the renderer's mesh geometry or verify Dying Light native physics.

## Geometry wire checks

Pure Java `GeometryPipeTests` covers async real byte writes, bounded enqueue, generation/revision enforcement, stream failure, cancellation and complete worker retirement. The guest session compiles optional geometry attachment and GET/block-change publication against Fabric, but source compilation does not establish loaded Mod or real-world behavior.



The independent [geometry codec](geometry.md) is compiled under strict Java 21 and C++20 rules. Tests validate bounded variable-length record parsing, malformed headers/text/padding/floats, air and retirement records, full-capacity data, complete cross-language golden bytes and Java→C++ subprocess exchange. Fabric sources additionally adapt authoritative collision snapshots into geometry records; GameTest execution is separately required.

The native `geometry-receiver` CTest verifies incremental and coalesced stream framing, byte-capacity bounds, invalid/oversized lengths, truncated EOF, replayed or missing revisions, old epochs, owner-thread refusal and staged collision cleanup. The real Java/C++ `GeometryReceiverProcessTests` also sends bytes through native anonymous pipes, including an actual `GeometryPipe` writer and negative malformed/partial-stream cases. Its reader runs only in a controlled process and does not attach to Dying Light.

## Client render model checks

The pure Java RenderFrameTests exercise fixed Minecraft baked-quad word decoding, finite geometry/UV and camera checks, source immutability, maximum 512 quads, logical frame/capture monotonicity, identity mismatch, wrong dimension, explicit revocation and cross-thread refusal. The client-only MinecraftRenderModels source is compiled against pinned Yarn/Fabric, with GameTest source for the actual model of a placed diamond block and a subsequent air state. Fabric compilation is not GameTest execution or Dying Light frame/depth correspondence. [Rendering scope](render-model.md)

## Render record contract checks

The registered [render record v1](render-wire.md) checks fixed-width little-endian Java/C++ codecs, complete 784-byte cross-language golden bytes, maximum 205,184-byte bounds, invalid versions/flags/text/padding/floats and early truncation refusal. Native `render-receiver` CTest verifies arbitrary fragmentation, identity changes, frame/capture monotonicity, staged position limits, truncation and EOF revocation. Pure Java `RenderPipelineTests` verifies bounded asynchronous output, source failure, wrong identities, stale capture, overflow, cancellation and worker retirement; `RenderReceiverProcessTests` sends genuine Java bytes and asynchronously published records into an independent native receiver. The Java-to-C++ native echo process verifies whole record bytes, an empty-model snapshot and a retirement record. A real Minecraft GameTest remains unrun; the cross-process suite does not establish texture pixels, projection alignment or host rendering.

## Source texture transport checks

The separate [TextureWire v1](texture-wire.md) verifies bounded 320-byte headers and 4-byte ARGB pixels, maximum 64 × 64 source images, zero-padded ASCII atlas/sprite IDs, signed-independent packed color preservation, UV finiteness, retire frames, malformed version/size/dimensions, and truncated input. A complete 336-byte Java/C++ reference record is compared byte-for-byte and the real native texture peer exchanges source images and retirement records with the Java process; it has no game dependencies. The Fabric compilation gate builds the client-only SpriteContents NativeImage accessor, real sprite source sampler and client GameTest source. These do not establish live Minecraft/Mixin loading or host GPU rendering.

## Bridge and guest checks

`python tools/test_bridge.py` compiles C++ with strict warnings and Java with `--release 21 -Xlint:all -Werror`, runs the wire/core/lifecycle suites, compares the complete 512-byte reference frame, and exercises three actual Java/C++ pipe scenarios against a controlled world. It checks command ordering, receipt replay/conflicts, generations, capacities, failed sends and cleanup. It does not instantiate Minecraft.

`PipeTests` additionally drives a native fault peer through fragmented frames, partial/empty EOF, input and output saturation, a broken write pipe, cancellation of blocked reads, an injected session timeout with an incomplete real pipe frame, concurrent send/close and repeated retirement. Controlled stream cases check a read completing after close, a pending cancellation and preserved cleanup failures. Each scenario checks worker termination; Linux also checks `/proc/self/fd` before and after it. A 32-cycle reconnect test rejects traffic from retired links and checks descriptor/thread counts without accepting accumulation. Platforms without `/proc/self/fd` explicitly report that descriptor measurement as unavailable. The native child and test diagnostics must terminate even when assertions fail.

`python tools/gradle.py build` is the separate Fabric compilation and JAR-inventory check. GameTest source is compiled by that build; execution requires explicit opt-in properties and local conditions described in [Local guest validation](guest-validation.md). Server tests cover canonical states and endpoint refusal; client tests cover an integrated world, real chunk observation, an independent state change, removal and screenshots. Ubuntu Fabric source compilation, GameTest source compilation, remapping and production JAR inspection passed [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451), with Minecraft and LAN execution disabled. Actual game tests, world/mode transitions, Windows Fabric builds and MSVC-specific native validation remain separate uncompleted acceptance records.

Ruff remains a required repository check. Bootstrap tests use synthetic archives and mocked downloads; they cannot establish that Maven resolution or a Gradle task graph succeeds. No automatic workflow accepts a Minecraft EULA or starts game tests.

## Evidence levels

| Level | What it establishes | What it does not establish |
| --- | --- | --- |
| Static/format check | Source/document/configuration properties | Runtime correctness |
| Generated-input unit test | Parser and boundary behavior for controlled bytes | Compatibility with a game engine |
| Process integration | Actual CLI or endpoint behavior across processes | Graphics or native host behavior |
| Native binary/graphics experiment | Observations for the precise binary/device tested | General game-build support |
| Real-game acceptance | Recorded behavior in the specified original runtimes | Other builds, modes or configurations |

Record the source commit, tool versions, input identities, environment, command, expected result, observations, and cleanup for a compatibility-changing experiment. For game tests also identify EXE/DLL hashes, graphics configuration and session mode. Do not publish executable files, saves or personal paths; use hashes and sanitized measurements.

Tests must reproduce the original defect before accepting a fix. Preserve existing assertions and compare normal, boundary and recovery paths. Avoid repeating an unchanged passing suite solely to increase test counts. The [baseline evidence](evidence/initial-baseline.md) records concrete initial results; later results belong to their PR and CI run.
