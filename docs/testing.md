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

## D3D11 depth attachment checks

Windows registers `d3d11-depth` in the existing native CTest stage, linked to the production depth attachment library and the system D3D11/D3DCompiler APIs. It creates a real WARP software device with controlled color/depth resources and P2 projection inputs. Pixel readback distinguishes an occluded far primitive, visible far pixels outside the occluder and a visible near primitive for normal and reverse Z; depth bytes before and after the read-only draw are compared. Source references, stale capture/configuration, current bindings, viewport, budgets and explicit retirement have separate cases. Private COM data sentinels test owned-view release and distinguish caller/context references from attachment references.

No game process or game asset participates. CPU readback, shader compilation, clears, bindings and flushes belong only to the test executable. Production attachment preparation observes metadata and creates a read-only view; it never draws, maps or changes pipeline bindings. A WARP pass does not prove Dying Light depth/pass correspondence, GPU-driver recovery or a safe game callback. [Supported layouts and outstanding acceptance](d3d11-depth.md)

## Geometry wire checks

Pure Java `GeometryPipeTests` covers async real byte writes, bounded enqueue, generation/revision enforcement, stream failure, cancellation and complete worker retirement. The guest session compiles optional geometry attachment and GET/block-change publication against Fabric, but source compilation does not establish loaded Mod or real-world behavior.



The independent [geometry codec](geometry.md) is compiled under strict Java 21 and C++20 rules. Tests validate bounded variable-length record parsing, malformed headers/text/padding/floats, air and retirement records, full-capacity data, complete cross-language golden bytes and Java→C++ subprocess exchange. Fabric sources additionally adapt authoritative collision snapshots into geometry records; GameTest execution is separately required.

The native `geometry-receiver` CTest verifies incremental and coalesced stream framing, byte-capacity bounds, invalid/oversized lengths, truncated EOF, replayed or missing revisions, old epochs, owner-thread refusal and staged collision cleanup. The real Java/C++ `GeometryReceiverProcessTests` also sends bytes through native anonymous pipes, including an actual `GeometryPipe` writer and negative malformed/partial-stream cases. Its reader runs only in a controlled process and does not attach to Dying Light.

The `geometry-exception` CTest injects four real allocation failures into geometry state decoding and projected-state copies. A bounded trace identifies the long canonical-state buffer request and rejects collisions with empty/move container bookkeeping before enabling faults. This avoids unintentionally selecting a Debug STL allocation inside a `noexcept` constructor. Each case checks the exact injected exception, invalidated owned geometry/projection, retained independent sources and successful retry without a partially committed capture history.

## Client render model checks

The pure Java RenderFrameTests exercise fixed Minecraft baked-quad word decoding, finite geometry/UV and camera checks, source immutability, maximum 512 quads, logical frame/capture monotonicity, identity mismatch, wrong dimension, explicit revocation and cross-thread refusal. The client-only MinecraftRenderModels source is compiled against pinned Yarn/Fabric, with GameTest source for the actual model of a placed diamond block and a subsequent air state. Fabric compilation is not GameTest execution or Dying Light frame/depth correspondence. [Rendering scope](render-model.md)

## Render record contract checks

The registered [render record v1](render-wire.md) checks fixed-width little-endian Java/C++ codecs, complete 784-byte cross-language golden bytes, maximum 205,184-byte bounds, invalid versions/flags/text/padding/floats and early truncation refusal. Native `render-receiver` CTest verifies arbitrary fragmentation, identity changes, frame/capture monotonicity, staged position limits, truncation and EOF revocation. Pure Java `RenderPipelineTests` verifies bounded asynchronous output, source failure, wrong identities, stale capture, overflow, cancellation and worker retirement; `RenderReceiverProcessTests` sends genuine Java bytes and asynchronously published records into an independent native receiver. The Java-to-C++ native echo process verifies whole record bytes, an empty-model snapshot and a retirement record. The real client failed before this model-sampling GameTest completed; the cross-process suite does not establish live texture pixels, projection alignment or host rendering.

## Source texture transport checks

The [TextureWire v1 and material contracts](texture-wire.md) verify bounded 320-byte headers and 4-byte ARGB pixels, maximum 64 × 64 source images, zero-padded ASCII atlas/sprite IDs, signed-independent packed color, valid UV, retirement and malformed/truncated input. Native `material-cache` and `material-receiver` CTests retain the ordered single-model API checks for completeness, unknown/duplicate sprites, identity/manifest refusal, fragmented records, partial EOF and thread ownership. `MaterialProcessTests` sends an authoritative model followed by its textures through a real Java→C++ process. The complete 336-byte Java/C++ reference record and texture-peer exchanges check exact codec bytes separately.

The `material-coordinator` CTest enumerates FIFO-preserving interleavings of multiple model and texture captures, including repeated positions, and tests every split point in its generated model/texture records. It checks texture-first isolation, complete-manifest availability, immediate old-material withdrawal, drain records that cannot resurrect replaced models, empty-model capture gaps, duplicates, foreign identities, malformed data and both-channel EOF/failure. Capacity cases exercise pending captures, current positions, retained record identities and charged storage independently; deadline cases reject partial-record trickling, unrelated progress and a regressed clock. Recovery reuses the same coordinator instance with a strictly newer epoch.

Each coordinator test instance has explicit `unique_ptr` ownership, preserving its original scope without accumulating the large bounded parser arrays in a single stack frame. This fixture correction does not raise the executable stack reservation or change production admission limits. The in-memory CTest and the independent OS-pipe process stage remain separately registered.

Java `MaterialPipelineTests` covers bounded asynchronous batches, saturation, source/reload cancellation and paired shutdown when either writer fails during write or flush. The owner makes no further publisher calls while the tests check both workers and endpoints. Additional cases exercise concurrent failures, registry registration/unregistration races, late stop-listener registration, reentrancy and retained cleanup errors.

The registered `material-coordinator-process-tests` stage runs [MaterialCoordinatorProcessTests](../guest/src/test/java/io/ranzevor/harrancraft/bridge/MaterialCoordinatorProcessTests.java) with [hc-material-coordinator-peer](../native/tests/material_coordinator_peer.cpp). Model records travel through child-process stdin and textures through an independent private named pipe on Windows or FIFO on POSIX. Native observations confirm bytes consumed from each channel and return complete material bytes for comparison. Scenarios include forced texture-first delivery by the actual publisher, independent partial records, multiple captures, replacement, missing/duplicate/foreign input, assembly timeout, both-channel EOF and resource withdrawal. Gated writes after native readiness force actual texture-queue saturation, paired cancellation and removal of the previously available material. Recovery starts new native processes, endpoints and publishers with newer epochs, rejects old-epoch records and occupies every registry slot to verify automatic unregistration. The harness checks child/reader termination and owned endpoint cleanup, preserving cleanup failures.

The Fabric compilation gate builds the client-only SpriteContents NativeImage accessor, real sprite source sampler, resource-reload listener and client GameTest source. The process and unit suites do not establish live Minecraft/Mixin loading, actual resource-pack reloads or Dying Light GPU rendering.

## Bridge and guest checks

`python tools/test_bridge.py` compiles C++ with strict warnings and Java with `--release 21 -Xlint:all -Werror`, runs the wire/core/lifecycle suites, compares the complete 512-byte reference frame, and exercises three actual Java/C++ pipe scenarios against a controlled world. It checks command ordering, receipt replay/conflicts, generations, capacities, failed sends and cleanup. It does not instantiate Minecraft.

`LifecycleTests` checks all 64 private-server admission flag combinations through initial handshakes and queued mutations in actual `Session` instances. Only ready private integrated mode may proceed; rejected states close the transport, remove subscriptions and prevent queued writes. The production Minecraft adapter and bounded GameTest readiness queries use the [verified pinned readiness meaning](validation-20261017.md#correct-integrated-server-readiness).

`PipeTests` additionally drives a native fault peer through fragmented frames, partial/empty EOF, input and output saturation, a broken write pipe, cancellation of blocked reads, an injected session timeout with an incomplete real pipe frame, concurrent send/close and repeated retirement. Controlled stream cases check a read completing after close, a pending cancellation and preserved cleanup failures. Each scenario checks worker termination; Linux also checks `/proc/self/fd` before and after it. A 32-cycle reconnect test rejects traffic from retired links and checks descriptor/thread counts without accepting accumulation. Platforms without `/proc/self/fd` explicitly report that descriptor measurement as unavailable. The native child and test diagnostics must terminate even when assertions fail.

In normal fault-peer scenarios, the peer completes its bounded diagnostic stream with an explicit terminal record and EOF before the cancellation worker forcibly terminates the still-owned child. The flood scenario keeps writing until the input pipe is closed and reports that precise closure, making close-before-kill ordering observable. On Windows it writes directly to the inherited pipe with synchronous [`WriteFile`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile#pipes). Only the documented [`ERROR_BROKEN_PIPE` (109, ended) and `ERROR_NO_DATA` (232, closing) statuses](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-) acknowledge closure; generic CRT `EINVAL` and other Win32 errors do not. POSIX retains the explicit `EPIPE` check. Unexpected diagnostics, incomplete evidence and read failures retain their original causes; process exit does not discard them. Negative cases verify a reserved native diagnostic-close failure exit and withhold delivery of an already-observed EOF to exercise the three-second cancellation deadline and unconditional owned-process termination. Diagnostic, cleanup and exit errors remain separately reportable. In the deliberately broken-output scenario, a buffered close may retry the failed flush; only the exact retained exception from that owned close, paired with the recorded output failure and no additional cleanup error, is expected. Every case still checks child, stream and worker retirement. The handshake belongs to the controlled test peer and does not add blocking work to the production `PipeLink` caller.

`python tools/gradle.py build` is the separate Fabric compilation and JAR-inventory check. GameTest source is compiled by that build; execution requires explicit opt-in properties and local conditions described in [Local guest validation](guest-validation.md). Server tests cover canonical states and endpoint refusal; client tests cover an integrated world, real chunk observation, an independent state change, removal and screenshots. Ubuntu Fabric source compilation, GameTest source compilation, remapping and production JAR inspection passed [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451), with Minecraft and LAN execution disabled. The [2026-10-17 local evidence](validation-20261017.md) separately records three actual server GameTests passing and a client run failing at first attachment. Complete client world/mode transitions, sampler execution and MSVC-specific native acceptance remain incomplete; direct Gradle task results do not establish a complete wrapper validation.

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
