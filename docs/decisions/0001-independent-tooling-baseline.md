# 0001 — Independent tooling baseline

Date: 2026-10-15. Scope: repository initialization and non-mutating development tools.

## Context

The target is full Minecraft × Dying Light 1 fusion. A game runtime and validated host interfaces are not prerequisites for every engineering task, but neither can be inferred from an existing overlay or a Windows build. The new repository needs a reproducible, useful first component without pretending to have a host adapter.

## Decision

Start with independent Python tooling for bounded, read-only PE identity and export inspection. Use the standard library at runtime, a versioned report schema, bilingual CLI resources and an isolated package verification path. Preserve the full native/Fabric/graphics/physics/damage roadmap; choose those toolchains with real endpoint work.

Keep the project self-contained: public contributor rules, code ownership, format configuration, security/support routes, documentation navigation and an explicit CI entry point. Existing external research is cited at fixed revisions, not copied as working HarranCraft code.

## Consequences

The current tool can run and be tested without a game or GPU. It supplies binary evidence for future probes, not authorization or compatibility decisions. Native API calls, shared-memory ABI and Minecraft version selection remain implementation work. This separation allows independent development to continue without bypassing host-mode and lifetime requirements.
