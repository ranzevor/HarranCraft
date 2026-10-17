# 相机标定与采集投影

[English](../camera-projection.md) · [架构](architecture.md) · [宿主观察](host-observation.md)

C++20 [相机数学模块](../../include/harrancraft/camera_projection.hpp)与[采集投影器](../../include/harrancraft/capture_projection.hpp)提供受控坐标标定及客体／宿主双相机投影，处理显式契约传入的 CPU 数据。投影器核实活跃宿主观察、完整客体材质及最新几何回执后，只提供一个投影采集。这些计算不证明已读取消光真实相机、取得场景深度、提交 GPU 绘制或注册原生物理资源。

## 版本、矩阵与单位

进程内投影契约为版本 **1**，不改变 [RenderWire v1](render-wire.md)、[TextureWire v1](texture-wire.md) 或 [GeometryWire v1](geometry.md)。现有渲染记录包含相机位置和 yaw/pitch，但缺少完整 view/projection 约定、FOV、裁剪距离、viewport 和配置修订号；几何记录只有代次／修订及方块身份，没有世界／相机／帧／采集键。

所有矩阵使用 `host::Matrix4`：16 个 double，按**列主序存储**，元素索引为 `matrix[column * 4 + row]`，乘以**列向量**。客体世界点 `g` 的计算顺序为：

```text
h = host_from_guest * [g.x, g.y, g.z, 1]
guest_clip = guest_projection * guest_view * [g.x, g.y, g.z, 1]
host_clip  = host_projection  * host_view  * h
```

客体坐标以方块为单位；`units_per_block` 明确一个方块对应多少宿主世界单位。宿主位置、宿主平移和宿主相机裁剪距离均使用该宿主单位，客体裁剪距离使用方块单位。契约不假定一个宿主单位就是一米。FOV 使用弧度，viewport 使用像素，NDC／深度值无量纲。

### 相似变换标定与误差

`Calibration` 提供维度、非零修订号、`host_from_guest`、有限正数 `units_per_block`、方向反转标志及绝对往返误差限。线性部分必须是 `s * Q`，其中 `s = units_per_block > 0`，`Q` 为正交矩阵；平移有限，仿射矩阵末行严格为 `[0, 0, 0, 1]`。归一化基向量的单位长度及两两正交误差不超过 `1e-9`，行列式按 `reverses_orientation` 对应 `+1` 或 `-1`，误差不超过 `4e-9`。拒绝剪切及非均匀缩放。维度是最多 64 字节、不含空白的非空 ASCII。

`map_point` 逐轴检查逆变换往返误差。`max_roundtrip_error` 默认 `1e-6` **客体方块单位**，须为有限正数；即使世界坐标很大，也保持绝对误差限。`unmap_point` 还将宿主残差除以 `units_per_block` 后检查。中间结果非有限或残差超限便拒绝该点，不随世界原点增大而放宽误差。

`correspondence_error` 接收 1–64 对客体／宿主对应点，返回以宿主单位计的最大欧氏残差。它检查调用方提供的标定，不拟合矩阵，也不自行决定验收阈值。

### 精确碰撞 AABB 适配

数学辅助函数支持带统一缩放的一般正交旋转／反射。现有 `geometry::CoordinateMap` 则通过带符号轴置换、逐轴缩放和平移保持轴对齐盒。

`calibration_from_axes` 只接受三个缩放绝对值严格相等的坐标映射。`axis_map` 要求标定矩阵每行恰有一个非零元素，其绝对值严格等于 `units_per_block`，且构成合法轴置换；不会将很小的旋转项近似成零。

`CaptureProjector` 要求该精确 `axis_map` 转换成功。一般旋转的相似变换可以用于 `project_pair`，但可能被关联来源的投影器拒绝。投影器不会将旋转盒放大成包围 AABB，也不创建有向碰撞体；它逐项比对映射结果与几何接收器已有的候选盒。

## 刚体相机与透视

每个 `CameraContract` 独立包含五项 `CaptureKey`、非零 `configuration_revision`、刚体 view 矩阵、世界位置、完整 `Perspective`、`Viewport` 及有限正数 `max_position_error`（默认 `1e-6`）。

view 矩阵必须有限且为仿射矩阵，左上基向量正交归一、行列式为 `+1`，采用与标定相同的 `1e-9` 基向量及 `4e-9` 行列式误差限。声明的相机位置经 view 变换后，各轴距原点不超过 `max_position_error`，误差采用该相机对应的单位。拒绝超出上述容差的缩放、剪切和反射。`view_matrix(position, right, up, forward, handedness)` 从显式给出的正交归一基构造该矩阵。

| Perspective 字段 | 契约 |
| --- | --- |
| `version` | 严格为 1 |
| `vertical_fov_radians` | 有限，严格大于 0 且小于 pi |
| `aspect` | 有限正数，与 viewport 宽／高之比的相对误差不超过 `1e-9` |
| `near_distance`、`far_distance` | 有限且 `0 < near < far`，不支持无穷远平面 |
| `handedness` | 左手 view 沿 +Z 看向前方，右手 view 沿 -Z 看向前方 |
| `depth_range` | `zero_to_one`（ZO）或 `minus_one_to_one`（NO） |
| `reverse_z` | 反转 near/far 的深度端点，同时保持 `near < far` |

令 `q = 1 / tan(vertical_fov_radians / 2)`、`n = near`、`f = far`；左手 view 的 `s = +1`，右手 view 的 `s = -1`，透视矩阵仅有以下非零项：

```text
P[0,0] = q / aspect
P[1,1] = q
P[2,2] = s * (d_far + (d_far - d_near) * n / (f - n))
P[2,3] = (d_near - d_far) * n * f / (f - n)
P[3,2] = s
```

这里 `P[row,column]` 是数学行列索引，与前述存储索引区分。非有限或数值退化的矩阵结果被拒绝。齐次 `w = s * view_z` 表示正向距离。

| NDC 约定 | 普通 near／far | 反向 Z 的 near／far |
| --- | --- | --- |
| ZO | 0／1 | 1／0 |
| NO | -1／1 | 1／-1 |

客体 view 基、FOV 及其他相机参数由调用方作为受控输入提供，不从现有渲染记录的 yaw/pitch 猜测公式。给定矩阵与样本匹配，不证明已采到任一游戏的真实相机。

## Viewport、裁剪边界及双相机结果

`Viewport` 包含有符号 32 位 `x/y`、正的无符号 32 位 `width/height`，以及 `top_left` 或 `bottom_left` 原点。右边界和纵向末端边界须能由有符号 32 位坐标表示。屏幕坐标描述连续的 viewport 边界，不做整数像素取整或半像素偏移。

clip 坐标有限且 `w > 0` 时，`screen_point` 计算：

```text
ndc = clip.xyz / clip.w
pixel_x = x + (ndc.x + 1) * width / 2
pixel_y = y + (1 - ndc.y) * height / 2     [top_left]
pixel_y = y + (ndc.y + 1) * height / 2     [bottom_left]
depth = ndc.z                            [ZO]
depth = ndc.z / 2 + 1/2                   [NO]
```

裁剪判断包含边界：`-w <= x <= w`、`-w <= y <= w`，ZO 的 `0 <= z <= w` 或 NO 的 `-w <= z <= w`。反向 Z 改变端点映射，不改变这些不等式。裁剪体外的点保留计算坐标并标记 `inside=false`，不钳制到 viewport 或深度区间。

`project_pair` 返回客体／宿主世界点、两组齐次 clip 向量，以及各自可选的 `ScreenPoint`。某台相机的相机平面上或相机后方点（`w <= 0`）没有该相机的屏幕点，但仍保留有限的 clip 向量。其他非有限投影或屏幕计算使整对结果被拒绝。模块不进行多边形裁剪、光栅化、场景可见性判断、深度缓冲比较或透视正确的材质插值。

## 精确来源关联

客体与宿主分别拥有**独立的** `epoch/world/camera/frame/capture` 五项键，双方数字不要求相同。各项均非零；`GeometryAssociation` 中客体身份还须符合 wire 的正有符号 64 位范围。

`GeometryAssociation` 显式将选定客体键关联到几何代次／修订号、维度及方块坐标、规范 BlockState：

- 几何代次必须等于客体代次；几何修订号须为正且能由有符号 64 位表示。
- 关联中的客体键必须等于传入客体相机键。
- 维度／状态为不含空白的非空 ASCII，分别最多 64／192 字节；维度还须等于标定维度。
- 位置／状态相同不能证明时序一致。由于 GeometryWire v1 缺少对应采集字段，该关联是调用方提供的受控断言。

每次 `capture`、存在活跃结果时的 `poll`，以及 `current_key`、`snapshot` 都检查真实来源状态：

| 来源 | 必需证据 |
| --- | --- |
| `host::Observer` | 在不变且可读的宿主证据下按精确宿主键取得快照；view 和位置与宿主契约严格相同 |
| 宿主投影矩阵 | 与生成透视矩阵逐元素相对误差不超过 `1e-6`，没有绝对误差下限；预期为零的项须严格为零 |
| `MaterialCoordinator` | 对应位置的完整活跃 snapshot 材质；客体键及规范状态严格相同；记录中相机 XYZ 与客体相机位置严格相同 |
| `GeometryReceiver` | 接收器活跃，最新解码回执为 snapshot，代次、修订、维度、坐标及状态均严格相同 |
| 非空几何 | 已有候选的修订／状态／盒数相同，各映射 AABB 与候选对应盒严格相同 |
| 空几何 | 存在明确的最新空 snapshot，且该位置没有暂存候选 |

`GeometryReceiver::last_record()` 保留一条已接受的解码记录，包含空 snapshot；缺少几何不等于空 snapshot。此后任何完整且已接受的几何记录，即使属于其他方块，也会令原关联过期；投影器下一次复核即撤销投影。撤销记录、EOF、接收失败或停止同样使来源失效。尚未形成另一条已接受记录的部分字节不会替换回执。

几何解码／应用期间发生分配或其他未分类异常时，接收器转为 `rejected`，清除候选、回执及解析存储，再抛出原始异常。失败的输入不能保留旧几何并继续将来源视为有效。

这一关联不负责同步 Minecraft 与消光时钟，也不证明真实运行时帧对应。受控测试中的宿主证据仍是测试输入；既有宿主构建／模式／读取阶段要求仍须取得真实游戏证据。

## 修订、所有权与撤销

投影器保留最后一次成功上下文，拒绝过期替换。后续成功采集须满足以下规则：

- 每台相机的代次不倒退。同一代次内世界不变，帧及采集号都严格增加，相机切换也不能绕过这两项递增。
- 同一代次、同一相机的配置修订号不能降低；复用修订号时，**Perspective 全字段、Viewport 全字段及 `max_position_error`** 必须相同。FOV、宽高比、裁剪距离、手性、深度范围、反向 Z、viewport 位置／尺寸／原点或误差限变化都须提高修订号。view 和位置属于逐帧姿态，可以随新帧变化而不改配置修订。
- 新代次或新相机可使用新的正配置修订号；独立客体／宿主键仍须匹配各自当前来源。
- 标定修订号不能降低，即使切换相机或代次也相同。复用修订号须使整个 `Calibration` 相同，包括维度、矩阵、比例、方向和往返误差限。
- 同一几何代次中，相邻成功投影采集的几何修订严格增加，不能将旧几何回执重新贴到一对新帧。

三个来源对象须比 `CaptureProjector` 存活更久，并与其共享所属线程。跨线程访问投影器抛出 `std::logic_error`，不会将操作派发给其他线程。I/O 端点与来源对象仍由调用方负责。

投影器只拥有**一个活跃投影采集**，最多 512 个四边形 × 4 个顶点，加 128 个盒 × 8 个角点，共 **3,072 个投影点**。四边形顶点和盒角点先从方块局部坐标平移到客体世界，再执行投影；每点同时包含双相机结果。结果还记录方向反转、递增序号、双方采集键、双方配置修订、标定修订及完整几何关联。方向标志不重新排列顶点，也不选择剔除模式。投影器不缓存精灵像素或材质；来源快照副本及调用方持有的结果副本不包含在这个点数界限内，该界限不是 RSS 上限。

新的 `capture` 尝试先撤销已有投影，即使替换随后被拒绝也不恢复旧结果；序号只在成功时增加。`poll` 在任一来源检查失败时清除活跃结果，遇到来源异常时先清除再继续抛出。`current_key` 或 `snapshot` 返回键／快照副本时若抛出异常，也会清除活跃结果并继续抛出原始异常。`stop` 清除活跃结果，但保留最后成功上下文及序号，不重置防重放检查，也不停止来源对象。

输入静默或来源变化后，所属线程仍须持续轮询。`snapshot(key, evidence, now)` 再次轮询，只为精确当前键返回副本。已返回副本无法被自动擦除，调用方须在消费前立即复核键及来源证据。`retained_points()` 只报告已存点数，不刷新来源有效性。投影器没有独立 TTL，而是在来源检查中观察材质期限。它是针对最新几何回执的单次标定结果，不持续渲染此前观察过的全部世界位置。

## 回归范围与依据

CMake 将 `camera-projection` 和 `capture-projection` 注册为原生 CTest。受控输入回归范围包含左右手、两种深度区间与反向 Z，刚体／相似变换拒绝，裁剪边界及相机后方点，viewport／配置变化，绝对坐标误差，精确 AABB 适配，独立采集身份，显式空几何，过期回执，来源撤销及所属线程访问。独立的 `geometry-exception` CTest 注入真实分配异常，检查接收器撤销、原始异常保留及投影器随后清除结果。

这些测试是受控 CPU 回归；各版本的实际 CI 证据记录在[开发 PR](https://github.com/ranzevor/HarranCraft/pull/2)。实际 Minecraft／GameTest 执行、真实相机／深度读取及宿主图形／物理验收仍未运行。

公式和约定边界参考以下原始资料，不新增运行依赖：

- [DirectXMath 约定](https://learn.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-getting-started)：该库采用行主序矩阵和行向量，与本契约对照时须转换相应转置／乘法顺序。
- [D3DXMatrixPerspectiveFovLH](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmatrixperspectivefovlh)：仅参考历史透视公式，项目不引入 D3DX。
- [XMMatrixPerspectiveFovLH](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmmatrixperspectivefovlh)：官方讨论反向 Z 的 near/far 设置；本契约保持 `near < far`，通过 `reverse_z` 选择深度端点。
- [Khronos ARB_clip_control](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_clip_control.txt)：裁剪体、深度范围与 viewport 约定。这些参考资料不证明任何消光构建的实际约定或资源读取方式。
