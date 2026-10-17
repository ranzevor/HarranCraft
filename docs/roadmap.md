# Roadmap

[简体中文](zh-CN/roadmap.md) · [Documentation](README.md)

Milestones describe required outcomes rather than a release schedule. Work independent of a missing game environment continues; passing an offline milestone does not promote an untested native interface.

| Milestone | Deliverable | Acceptance boundary |
| --- | --- | --- |
| H0 — Repository and binary identity | Public contribution/format/CI baseline and read-only PE inspector | Reproducible source checks, malformed inputs, locale/schema tests and an installed-wheel CLI; current implementation |
| H1 — Native observation | Exact image identities, session state, callback phase, camera and original-world queries | Explicit non-mutating observation, transition handling and safe teardown; real host execution still required |
| H2 — Real Fabric guest | Working Minecraft producer/consumer; repeated block placement/removal and real render/geometry output | Genuine Fabric run, protocol consistency, bounded queues, generation and reconnect tests; can progress independently of H1 where appropriate |
| H3 — Shared scene | Correct scene occlusion and native block collision in the same place | Actual host/guest rendering, player/enemy/projectile blocking and removal; navigation recorded separately |
| H4 — Damage and TNT | Two-way nonlethal damage and explosions | Before/after authoritative health, attribution, duplicates, death and reconnect behavior; no Kill-based substitute |
| H5 — Playable systems | Input return, inventory, items, building and creatures | End-to-end play through both original runtimes with resource/performance measurements |
| H6 — Persistence and delivery | Persistent bridge state, installation, upgrade, removal and exit recovery | Disposable test data, compatibility/recovery evidence and a separately approved release |

The isolated native geometry ledger stages mapped AABBs with bounded ownership but has no Dying Light collision registration, physics filtering or game-safe cleanup implementation.

## H2 acceptance sequence

**H2-A — Block events and bidirectional IPC:** source implementation and controlled Java/C++ process tests are available. Pinned dependency resolution, Fabric compilation and production JAR checks passed on Ubuntu/JDK 21 in [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451). Complete the remaining [local validation](guest-validation.md): server/client GameTests, Windows Fabric build, screenshots and real world/mode transitions. Linux and Windows/MinGW native process checks retain independent results for each revision in [PR #2](https://github.com/ranzevor/HarranCraft/pull/2). The guest accepts embedded private pipes; a user launcher and Dying Light endpoint are not part of this batch.

**H2-B — Real guest graphics output:** Client baked-model sampling and the independent Java/C++ render record now have an optional bounded asynchronous publisher and native candidate receiver with revoked frame identity; loaded sprite-source pixels now support bounded async batch publication and native completeness-gated CPU caching with resource-reload revocation; final GPU atlas, animation, actual Dying Light draw calls, calibrated camera/depth and real-game validation remain separate. Complete H2-A game acceptance, transport real render data with exact frame/lifetime pairing, and prove host view/projection/depth correspondence before H3 claims. Fluids, block entities, final lighting and host occlusion remain separate.

H1 observation experiments remain independently testable. Keep host-changing code gated by actual build, mode and engine-phase evidence. Continue source and protocol work without waiting for an unrelated game environment.

The long-term scope remains full dual-runtime fusion. Replacing models/textures, window overlays and guest-only interactions cannot satisfy H3–H6.
