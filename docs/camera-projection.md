# Camera calibration and capture projection

[简体中文](zh-CN/camera-projection.md) · [Architecture](architecture.md) · [Host observation](host-observation.md)

The C++20 [camera mathematics](../include/harrancraft/camera_projection.hpp) and [capture projector](../include/harrancraft/capture_projection.hpp) provide controlled coordinate calibration and paired guest/host projections. They operate on CPU data supplied through explicit contracts. The projector validates an active host observation, complete guest material and the latest geometry receipt before exposing one projected capture. These calculations do not establish a Dying Light camera reader, scene-depth access, GPU submission or native physics registration.

## Version, matrices and units

The in-process projection contract is version **1**. It does not change [RenderWire v1](render-wire.md), [TextureWire v1](texture-wire.md) or [GeometryWire v1](geometry.md). The render record carries camera position and yaw/pitch, but lacks a complete view/projection convention, FOV, clip distances, viewport and configuration revision. Geometry records have epoch/revision and block identity, without world/camera/frame/capture keys.

All matrices use `host::Matrix4`: 16 doubles in **column-major storage**, with entry `matrix[column * 4 + row]`, multiplied by **column vectors**. For a guest world point `g`:

```text
h = host_from_guest * [g.x, g.y, g.z, 1]
guest_clip = guest_projection * guest_view * [g.x, g.y, g.z, 1]
host_clip  = host_projection  * host_view  * h
```

Guest positions use block units. `units_per_block` states how many host world units represent one block; host positions, host translation and host camera clip distances use those host units. Guest clip distances use block units. The contract does not assume that a host unit is a metre. FOV uses radians, viewport coordinates use pixels, and NDC/depth values are dimensionless.

### Similarity calibration and error

`Calibration` supplies a dimension, nonzero revision, `host_from_guest`, positive finite `units_per_block`, an orientation flag and an absolute round-trip tolerance. Its linear part must be `s * Q`, where `s = units_per_block > 0` and `Q` is orthogonal. Translation is finite; the affine last row must be exactly `[0, 0, 0, 1]`. The normalized basis has unit lengths and pairwise orthogonality within `1e-9`; its determinant must match `+1` or `-1`, as selected by `reverses_orientation`, within `4e-9`. Shear and nonuniform scale are rejected. The nonempty dimension is at most 64 ASCII bytes without whitespace.

`map_point` checks the inverse round trip separately on each axis. `max_roundtrip_error` defaults to `1e-6` **guest block units**, must be finite and positive, and remains an absolute tolerance at large world coordinates. `unmap_point` also checks the host residual divided by `units_per_block`. Non-finite intermediates or an excessive residual reject the point; a large world origin does not relax the error allowance.

`correspondence_error` accepts 1–64 supplied guest/host point pairs and returns the maximum Euclidean residual in host units. It checks a supplied calibration; it does not fit a matrix or decide an acceptance threshold.

### Exact collision AABB adaptation

The mathematical helpers accept a general orthogonal rotation/reflection with uniform scale. The existing `geometry::CoordinateMap` instead preserves axis-aligned boxes through signed axis permutations, per-axis scale and translation.

`calibration_from_axes` accepts that map only when all three absolute scales are exactly equal. `axis_map` accepts a calibration only when each row has exactly one nonzero entry whose magnitude is exactly `units_per_block`, with a valid axis permutation. Near-zero rotation terms are not rounded away.

`CaptureProjector` requires this exact `axis_map` conversion. A rotated similarity may be valid for `project_pair` while being rejected by the source-bound projector. The projector neither enlarges rotated boxes into enclosing AABBs nor creates oriented colliders. It compares the mapped boxes with the geometry receiver's existing candidates.

## Rigid cameras and perspective

Each `CameraContract` contains its own five-part `CaptureKey`, nonzero `configuration_revision`, rigid view matrix, world position, full `Perspective`, `Viewport` and positive finite `max_position_error` (default `1e-6`).

The view matrix must be finite and affine, with an orthonormal upper-left basis of determinant `+1`, using the same `1e-9` basis and `4e-9` determinant tolerances as calibration. Transforming the declared camera position through the view matrix must place it at the origin within `max_position_error` on each axis, in that camera's units. Scale, shear and reflection outside these tolerances are rejected. `view_matrix(position, right, up, forward, handedness)` constructs this matrix from an explicitly supplied orthonormal basis.

| Perspective field | Contract |
| --- | --- |
| `version` | Exactly 1 |
| `vertical_fov_radians` | Finite, strictly between 0 and pi |
| `aspect` | Finite and positive; agrees with viewport width/height within relative `1e-9` |
| `near_distance`, `far_distance` | Finite `0 < near < far`; no infinite far plane |
| `handedness` | Left-handed view looks along +Z; right-handed view looks along -Z |
| `depth_range` | `zero_to_one` (ZO) or `minus_one_to_one` (NO) |
| `reverse_z` | Reverses near/far depth endpoints while keeping `near < far` |

For `q = 1 / tan(vertical_fov_radians / 2)`, `n = near`, `f = far`, and `s = +1` for left-handed or `-1` for right-handed view space, the only nonzero perspective entries are:

```text
P[0,0] = q / aspect
P[1,1] = q
P[2,2] = s * (d_far + (d_far - d_near) * n / (f - n))
P[2,3] = (d_near - d_far) * n * f / (f - n)
P[3,2] = s
```

Here `P[row,column]` is mathematical indexing, independent of the storage indexing above. Non-finite or numerically degenerate results are rejected. Homogeneous `w = s * view_z` is positive forward distance.

| NDC convention | Normal near / far | Reverse-Z near / far |
| --- | --- | --- |
| ZO | 0 / 1 | 1 / 0 |
| NO | -1 / 1 | 1 / -1 |

The guest view basis, FOV and other camera parameters are controlled caller inputs. No yaw/pitch formula is inferred from the existing render record. Agreement between supplied matrices and a fixture is not proof that either game's real camera was sampled.

## Viewport, clip boundaries and paired results

`Viewport` has signed 32-bit `x/y`, positive unsigned 32-bit `width/height` and `top_left` or `bottom_left` origin. The right and final vertical edges must fit signed 32-bit coordinates. Screen coordinates describe continuous viewport edges, without integer pixel rounding or a half-pixel offset.

For finite clip coordinates with `w > 0`, `screen_point` computes:

```text
ndc = clip.xyz / clip.w
pixel_x = x + (ndc.x + 1) * width / 2
pixel_y = y + (1 - ndc.y) * height / 2     [top_left]
pixel_y = y + (ndc.y + 1) * height / 2     [bottom_left]
depth = ndc.z                            [ZO]
depth = ndc.z / 2 + 1/2                   [NO]
```

The inclusive clip test is `-w <= x <= w`, `-w <= y <= w`, and either `0 <= z <= w` for ZO or `-w <= z <= w` for NO. Reverse Z changes the endpoints, not these inequalities. Points outside the clip volume retain their calculated coordinates and have `inside=false`; values are not clamped into the viewport or depth interval.

`project_pair` returns guest/host world points, both homogeneous clip vectors and separate optional guest/host `ScreenPoint` values. A camera-plane or behind-camera point (`w <= 0`) has no screen point for that camera, while its finite clip vector is retained. Other non-finite projection or screen calculations reject the pair. No polygon clipping, rasterization, visibility test against a scene, depth-buffer comparison or perspective-correct material interpolation is performed.

## Exact source association

Guest and host identities are **independent** `epoch/world/camera/frame/capture` keys. Their numbers need not equal one another. Every component is nonzero; the guest identity in `GeometryAssociation` also fits positive signed 64-bit wire fields.

`GeometryAssociation` explicitly binds the chosen guest key to geometry epoch/revision, dimension and block coordinates, plus canonical block state:

- Geometry epoch must equal the guest epoch. Geometry revision must be positive and fit signed 64-bit.
- The association's guest key must equal the supplied guest camera key.
- Dimension/state are nonempty ASCII without whitespace, at most 64/192 bytes. Dimension must also equal the calibration dimension.
- Position/state equality alone does not establish timing. The association is a caller-supplied controlled assertion because GeometryWire v1 lacks the missing capture fields.

Each `capture`, active `poll`, `current_key` and `snapshot` checks all live sources:

| Source | Required evidence |
| --- | --- |
| `host::Observer` | An exact host key under unchanged, readable host evidence; view and position exactly match the host contract |
| Host projection matrix | Matches the generated perspective entry by entry within relative `1e-6`, with no absolute tolerance floor; expected zeros remain exact zeros |
| `MaterialCoordinator` | Complete, active snapshot material at the associated position; exact guest key and canonical state; record camera XYZ exactly match the guest camera position |
| `GeometryReceiver` | Active receiver and latest decoded receipt is a snapshot with exact epoch, revision, dimension, coordinates and state |
| Nonempty geometry | Existing candidate revision/state/count match, and every mapped AABB exactly matches the candidate's corresponding box |
| Empty geometry | An explicit latest empty snapshot exists and the position has no staged candidate |

`GeometryReceiver::last_record()` retains one decoded accepted record, including an empty snapshot. Missing geometry is not an empty snapshot. Any later complete accepted geometry record, even for another block, makes the earlier association stale; its next projector check withdraws the projection. Retirement, EOF, receiver failure or stopping also invalidate the source. Partial bytes that have not yet formed another accepted record do not replace the receipt.

An allocation or other unclassified exception during geometry decoding/application retires the receiver as `rejected`, clears candidates, the receipt and parser storage, then rethrows the original exception. A failed feed cannot leave earlier geometry available as if the source were still valid.

This association does not synchronize Minecraft and Dying Light clocks or certify runtime frame correspondence. Host evidence in controlled tests remains test input; the existing host build/mode/read-phase requirements still need real-game evidence.

## Revisions, ownership and withdrawal

The projector keeps the last successful context to reject stale replacements. A subsequent successful capture must satisfy all of these rules:

- Each camera epoch is nondecreasing. Within the same epoch, world is unchanged and both frame and capture strictly increase, including on a camera switch.
- For the same epoch and camera, configuration revision cannot decrease. Reusing it requires equality of **every Perspective field, every Viewport field and `max_position_error`**. FOV, aspect, clip distances, handedness, depth range, reverse Z, viewport position/size/origin or tolerance changes require a higher revision. View and position are per-capture pose and may change with a new frame without a configuration revision change.
- A new epoch or camera may start a new positive camera configuration revision. The independent guest/host keys still have to match their current sources.
- Calibration revision cannot decrease, even across camera/epoch changes. Reusing it requires the entire `Calibration` value to match, including dimension, matrix, scale, orientation and round-trip tolerance.
- Within the same geometry epoch, geometry revision strictly increases between successful projected captures. An old geometry receipt cannot be attached to a new pair of frames.

All three source objects must outlive `CaptureProjector` and share its owner thread. Cross-thread projector access throws `std::logic_error`; it does not dispatch work to another thread. I/O endpoints and the source objects remain caller-owned.

The projector owns **one active projected capture**, bounded by 512 quads × 4 vertices plus 128 boxes × 8 corners = **3,072 projected points**. Quad vertices and box corners are translated from block-local coordinates before projection. Each point contains both camera results. The result also records orientation reversal, an increasing serial, both capture keys, both configuration revisions, calibration revision and the full geometry association. The orientation flag does not reorder vertices or select a culling mode. The projector retains no sprite pixels or material cache; source snapshot copies and caller-held result copies are separate from this point-count bound, which is not an RSS limit.

A new `capture` attempt first withdraws the existing projection, including when the replacement is rejected. The serial increases only on success. `poll` clears active output when any source check fails and clears it before propagating a source exception. If returning a key or snapshot copy from `current_key` or `snapshot` throws, active output is also cleared and the original exception is rethrown. `stop` clears active output while preserving the last successful context and serial; it does not reset replay checks or stop the source objects.

The owner must continue polling during input silence and after source changes. `snapshot(key, evidence, now)` polls again and returns a copy only for the exact current key. A previously returned copy cannot be automatically erased: the caller must recheck the key and source evidence immediately before consuming it. `retained_points()` only reports stored points and does not refresh source validity. The projector adds no independent time-to-live; it observes material deadlines through source checks. This is a single latest-receipt calibration result, not a persistent renderer of all previously observed world positions.

## Regression scope and references

CMake registers `camera-projection` and `capture-projection` as native CTests. Their controlled-input regression scope includes handedness and both depth ranges with reverse Z, rigid/similarity rejection, clip edges and behind-camera points, viewport/configuration changes, absolute coordinate error, exact AABB adaptation, independent capture identities, explicit empty geometry, stale receipts, source revocation and owner-thread access. The separate `geometry-exception` CTest injects actual allocation failures, checking receiver retirement, preservation of the original exception and subsequent projector withdrawal.

These are controlled CPU regressions. Revision-specific CI evidence is recorded in the [development PR](https://github.com/ranzevor/HarranCraft/pull/2). Actual Minecraft/GameTest execution, live camera/depth acquisition and host graphics/physics acceptance remain unrun.

The formulas and convention boundaries use these primary references; they add no runtime dependency:

- [DirectXMath conventions](https://learn.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-getting-started): row-major matrices and row vectors in that library; comparisons with this contract require the corresponding transpose/order conversion.
- [D3DXMatrixPerspectiveFovLH](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmatrixperspectivefovlh): historical perspective formula reference only; this project does not add D3DX.
- [XMMatrixPerspectiveFovLH](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmmatrixperspectivefovlh): official reverse-Z near/far discussion; this contract instead keeps `near < far` and selects depth endpoints with `reverse_z`.
- [Khronos ARB_clip_control](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_clip_control.txt): clip-volume, depth-range and viewport convention definitions. These references do not establish the conventions or resource access of any Dying Light build.
