# Read-only D3D11 depth attachment

[简体中文](zh-CN/d3d11-depth.md) · [Camera projection](camera-projection.md) · [Host observation](host-observation.md)

The Windows C++20 [depth attachment](../include/harrancraft/d3d11_depth.hpp) prepares a read-only depth-stencil view of a caller-identified render pass. It associates the view with a complete host build session, an exact camera capture and the currently selected render target. This provides a Direct3D resource contract for later depth-tested rendering. It does not identify the Dying Light scene pass or submit Minecraft geometry.

## Inputs and accepted source

The in-process contract has version **1**. `D3D11DepthAttachment` borrows a `host::BuildSession`, which must outlive it and share its owner thread. The source session must already contain an accepted EXE-plus-DLL build profile and a current camera observation under `single_player / playing / verified_engine_read` evidence. An empty allowlist accepts no build.

`prepare(evidence, camera, generation, context, color_slot)` requires an immediate `ID3D11DeviceContext` supplied by its owning caller, on a device with feature level **11.0 or higher**. The color slot is an explicit value from 0 through 7; it is not selected by searching for the largest render target. The helper queries output-merger render-target/depth-stencil views and the rasterizer viewport, without changing pipeline bindings.

The caller must establish that this context, selected color target, depth surface and camera refer to the same scene pass and capture. A matching surface size, frame number, window thread or Present callback does not establish that association or a safe engine phase. The helper does not discover a context, hook a swap chain, scan game memory or choose a callback.

The camera must satisfy the [P2 camera contract](camera-projection.md), use `zero_to_one` clip depth and a `top_left` viewport, and match the current build-session snapshot. Position and view are exact matches. The snapshot projection must agree with the generated perspective within relative `1e-6` per entry, without an absolute tolerance floor; expected zero entries remain zero.

Exactly one viewport must be active. Its position and dimensions must match the camera viewport, remain inside the selected surfaces and use `MinDepth=0`, `MaxDepth=1`. Both normal and reverse Z use this same viewport depth interval. Multiple viewports, fractional viewport differences, a missing target or a different camera configuration are rejected.

### Texture and view layout

The selected color and depth sources must be `ID3D11Texture2D` resources from the context device, with matching width, height, sample count and quality. Each dimension is 1–16,384 texels; each resource has one mip level, one array slice, `D3D11_USAGE_DEFAULT`, no CPU access and the required render-target or depth-stencil bind flag. The views select mip zero through `TEXTURE2D`, or use the corresponding `TEXTURE2DMS` dimension for multisampling. Sample count is a power of two from 1 through 32, quality is zero, and the device must report support for that count on both typed view formats. Arrays, cube maps, additional mip levels and unsupported color formats are rejected rather than estimated from incomplete metadata.

The selected color view accepts these format families. The texture format must equal the view format or the listed typeless format:

| Color view format | Accepted typeless texture format | Bytes per texel/sample |
| --- | --- | --- |
| `R8G8B8A8_UNORM`, `R8G8B8A8_UNORM_SRGB` | `R8G8B8A8_TYPELESS` | 4 |
| `B8G8R8A8_UNORM`, `B8G8R8A8_UNORM_SRGB` | `B8G8R8A8_TYPELESS` | 4 |
| `B8G8R8X8_UNORM`, `B8G8R8X8_UNORM_SRGB` | `B8G8R8X8_TYPELESS` | 4 |
| `R10G10B10A2_UNORM` | `R10G10B10A2_TYPELESS` | 4 |
| `R11G11B10_FLOAT` | Typed resource only | 4 |
| `R16G16B16A16_FLOAT` | `R16G16B16A16_TYPELESS` | 8 |
| `R32G32B32A32_FLOAT` | `R32G32B32A32_TYPELESS` | 16 |

The depth view has an explicit typed format. Its texture may use that same typed format or the corresponding typeless resource format:

| Depth view format | Accepted typeless texture format | Bytes per texel/sample | Read-only flags on the prepared view |
| --- | --- | --- | --- |
| `D16_UNORM` | `R16_TYPELESS` | 2 | Depth |
| `D24_UNORM_S8_UINT` | `R24G8_TYPELESS` | 4 | Depth and stencil |
| `D32_FLOAT` | `R32_TYPELESS` | 4 | Depth |
| `D32_FLOAT_S8X24_UINT` | `R32G8X24_TYPELESS` | 8 | Depth and stencil |

Names in both tables omit the `DXGI_FORMAT_` prefix. A typeless resource does not make a typeless DSV valid. Unknown DSV flags and a stencil read-only flag on a format without stencil are rejected. The helper creates a separate DSV with `D3D11_DSV_READ_ONLY_DEPTH` and, for a stencil-bearing format, `D3D11_DSV_READ_ONLY_STENCIL`. It neither changes the original DSV nor creates a new depth texture, CPU image, shader-resource view or resolved depth copy.

## Capture identity and replacement

`DepthKey` contains the contract version, a nonzero attachment generation, the host `epoch/world/camera/frame/capture` key, camera configuration revision and selected color slot. Each successful attachment uses a strictly larger generation over the object's entire lifetime.

The camera epoch cannot decrease. Within an epoch, the world remains unchanged and both frame and capture strictly increase between successful preparations, including on a camera switch. For the same epoch and camera, configuration revisions cannot decrease; reuse requires all perspective and viewport fields and the camera-position tolerance to remain equal. A new camera or epoch may start a new positive configuration revision. Per-capture view and position may change with a new capture.

Every `prepare` attempt on the owner thread first retires the current attachment. A rejected replacement does not restore the old view and does not advance the successful generation/camera history. `stop` releases the active attachment while preserving that history. Recovery therefore requires a valid fresh capture and generation; stopping is not a way to replay an old result.

`access(key, evidence, camera, context)` checks the exact key and full camera contract again, obtains the current build-session snapshot, checks the device and re-queries the active bindings and viewport. The selected color view must still have the same COM identity. The depth view may be the original writable DSV, the prepared read-only DSV or another view of the same underlying depth resource and layout. Only the depth/stencil read-only flag differences are permitted; resource identity, format, dimension and selected subresource must still agree.

An owner-thread access failure retires the active attachment. A new host observation, changed world/mode, revoked build session, changed binding, viewport mismatch, different device or reported device-removal error prevents use of the old view. The helper does not poll automatically: the caller must call `access` immediately before consumption and after relevant source changes.

The build check uses the current `BuildSession` state. It does not rehash files or independently observe newly loaded modules. The adapter must refresh the full build measurement outside frame callbacks and withdraw the session when that measurement changes or becomes unavailable. Unreported changes cannot be detected from a cached snapshot alone.

## Ownership, bounds and operation results

On success, the attachment owns references to the source device, selected color view and prepared read-only DSV. The DSV keeps its depth resource alive. Temporary interfaces returned by binding/resource queries are released on both success and failure. The context and `BuildSession` remain caller-owned.

`DepthAccess::view` is borrowed. It remains valid only until an owner-thread operation retires or replaces the attachment, or the attachment is destroyed. Consumers that acquire their own COM references or bind the view into a context own those additional lifetimes. `stop` cannot release a consumer's reference or unbind its context. Before resizing, replacing or destroying host resources, the consumer must relinquish its own bindings and references as well as retire the attachment.

The constructor's source budget defaults to **256 MiB**, must be positive and cannot exceed **1 GiB**; an invalid budget throws `std::invalid_argument`. `DepthDescription::source_bytes` accounts for the selected color texture plus depth texture:

```text
source_bytes = width * height * samples * (color_bytes + depth_bytes)
```

The format sizes come from the tables above. The bound does not measure driver allocation granularity, compression, residency, shared/context references, other render targets or process RSS. No CPU pixel buffer is retained. Keeping a view alive may extend the source allocation's lifetime even though the helper does not allocate a texture.

`DepthResult` distinguishes `ready`, `inactive`, invalid input/camera, stale capture, unsupported context/surface, missing or mismatched target, invalid viewport, exceeded budget, changed/lost device and Direct3D API failure. A description is exposed only for a successful attachment; a failed access returns no usable view. The `HRESULT` field preserves an available API error independently of the contract status.

Construct, use and destroy the attachment on the context's owner thread. `prepare`, `access` and `stop` reject cross-thread access with `std::logic_error` before touching its state or Direct3D objects. Destruction releases owned references without throwing; it does not dispatch cleanup to another thread. The owner must also ensure exclusive legal use of the supplied immediate context, including devices created with `D3D11_CREATE_DEVICE_SINGLETHREADED`; thread identity does not prove that another host component is not accessing it.

The helper never binds a target, selects a depth-stencil state, draws, clears, copies, resolves, maps or waits for GPU results. Creating and releasing D3D views still has driver-dependent cost; the contract is not a hard latency guarantee. The caller must establish suitable preparation/release phases. A read-only DSV also does not supply a rendering permission or restore state changed by a separate consumer.

For a later consumer, `DepthDescription::comparison` is `LESS_EQUAL` for normal Z and `GREATER_EQUAL` for reverse Z. The helper reports that choice without installing it. A consumer must independently validate depth-write policy, stencil, culling, blend state and restoration of all state it changes. Read-only depth preserves the existing scene depth; it does not resolve ordering or mutual occlusion among newly submitted guest surfaces by itself.

## Controlled validation and host acceptance

The [Windows regression](../native/tests/d3d11_depth_test.cpp) is registered as the `d3d11-depth` CTest and requests a real Direct3D 11 WARP device at feature level 11.0. Its controlled scene checks normal/reverse-Z foreground and background results against a supplied wall, together with unchanged depth data. The test owns its drawing, staging and readback operations; those operations are not part of `D3D11DepthAttachment`.

The regression also covers supported depth-view flags, source-layout and viewport rejection, capture/revision replay, binding changes, build-session withdrawal, owner-thread misuse and recovery. COM private-data lifetime counters verify reference release after stop, destruction and rejected replacements. The test relinquishes its own references/bindings and flushes deferred destruction before checking final release. Revision-specific execution results belong in the [development PR](https://github.com/ranzevor/HarranCraft/pull/2); a built test executable alone is not an executed WARP test.

WARP exercises the Windows D3D runtime with controlled resources. Actual hardware device removal, driver failure, GPU memory residency and timing require separate tests. A manufactured stale key or different device is not a hardware device-loss experiment.

Real Dying Light integration still requires an approved complete game build, verified context-thread and engine phases, actual camera/pass/depth correspondence, and resource release around resize, loading and shutdown. The current perspective contract does not represent temporal jitter, asymmetric/off-centre projection or an infinite far plane; TAA/jitter cannot be silently discarded to make matrices match. A host using those conventions, a different viewport depth interval or additional surface formats needs an explicit contract extension and regressions before attachment. This module does not register native colliders, implement damage/TNT, or establish a playable dual-runtime build.

## Direct3D references

- [OMGetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omgetrendertargets): current output-merger views and references returned to the caller.
- [RSGetViewports](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rsgetviewports): active viewport count and descriptors.
- [CreateDepthStencilView](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilview) and [DSV descriptor](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_depth_stencil_view_desc): resource/view construction and typed depth formats.
- [Read-only DSV flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_dsv_flag): independent depth and stencil write protection.
- [Texture2D descriptor](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_texture2d_desc): texture dimensions, mip/array layout and sample description.
- [GetDeviceRemovedReason](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-getdeviceremovedreason): device failure result reporting.
- [Direct3D 11 threading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro): immediate-context concurrency requirements.
- [OMSetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets): additional references retained by a consumer's bindings.
- [SetPrivateDataInterface](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicechild-setprivatedatainterface) and [Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush): lifetime probes and deferred destruction in isolated tests.
- [WARP](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp): Windows software rasterization through Direct3D.
