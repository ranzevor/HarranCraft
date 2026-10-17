# D3D11 只读深度视图

[English](../d3d11-depth.md) · [相机投影](camera-projection.md) · [宿主观察](host-observation.md)

Windows C++20 [深度视图模块](../../include/harrancraft/d3d11_depth.hpp) 为调用方已识别的渲染 pass 准备只读深度／模板视图，将它与完整宿主构建会话、精确相机采集及当前选定的颜色目标关联。这为后续使用深度测试的渲染提供 Direct3D 资源契约；模块不识别消光场景 pass，也不提交 Minecraft 几何。

## 输入与来源条件

进程内契约为版本 **1**。`D3D11DepthAttachment` 借用 `host::BuildSession`，来源须比它存活更久，并共享所属线程。构建会话须已接受完整 EXE＋DLL 构建清单，且在 `single_player / playing / verified_engine_read` 证据下保留当前相机观察。空允许列表不接受任何构建。

`prepare(evidence, camera, generation, context, color_slot)` 要求调用方提供由其负责的 immediate `ID3D11DeviceContext`，设备 feature level 须为 **11.0 或更高**。颜色槽位由调用方显式选定，范围为 0—7，不通过寻找最大渲染目标推断。模块查询 output-merger 的颜色／深度视图和 rasterizer 的 viewport，不改变管线绑定。

调用方须证明该 context、颜色目标、深度表面与相机属于同一场景 pass 和采集。尺寸相同、帧号相同、窗口线程或 Present 回调均不能证明这种关联或引擎安全阶段。模块不发现 context、不挂接 swap chain、不扫描游戏内存，也不选择回调。

相机须满足 [P2 相机契约](camera-projection.md)，使用 `zero_to_one` 裁剪深度和 `top_left` viewport，并匹配构建会话的当前快照。位置与 view 严格相同；快照投影与生成透视矩阵逐项相对误差不超过 `1e-6`，没有绝对误差下限，预期为零的项须仍为零。

当前必须恰有一个 viewport。位置与尺寸须匹配相机 viewport，落在选定表面范围内，且 `MinDepth=0`、`MaxDepth=1`。普通和反向 Z 都使用这一 viewport 深度区间。多个 viewport、带小数的位置／尺寸差异、缺失目标或不同相机配置均拒绝。

### 纹理与视图布局

选定颜色和深度来源须为 context 对应设备的 `ID3D11Texture2D`，宽、高、采样数和质量匹配。各维度范围为 1—16,384 texel；每个资源只有一个 mip 层级和一个数组切片，使用 `D3D11_USAGE_DEFAULT`，没有 CPU 访问，并具有所需的颜色目标或深度／模板 bind flag。视图通过 `TEXTURE2D` 选择 mip 0，或对多重采样使用对应的 `TEXTURE2DMS` 维度。采样数为 1—32 中的 2 的整数次幂，质量为 0，设备须报告两种 typed 视图格式都支持该采样数。拒绝数组、立方体、多 mip 及未支持的颜色格式，不从不完整元数据估算。

选定颜色视图接受下列格式。纹理须使用与视图相同的格式或表中对应的 typeless 格式：

| 颜色视图格式 | 接受的 typeless 纹理格式 | 每 texel／sample 字节数 |
| --- | --- | --- |
| `R8G8B8A8_UNORM`、`R8G8B8A8_UNORM_SRGB` | `R8G8B8A8_TYPELESS` | 4 |
| `B8G8R8A8_UNORM`、`B8G8R8A8_UNORM_SRGB` | `B8G8R8A8_TYPELESS` | 4 |
| `B8G8R8X8_UNORM`、`B8G8R8X8_UNORM_SRGB` | `B8G8R8X8_TYPELESS` | 4 |
| `R10G10B10A2_UNORM` | `R10G10B10A2_TYPELESS` | 4 |
| `R11G11B10_FLOAT` | 仅同格式 typed 资源 | 4 |
| `R16G16B16A16_FLOAT` | `R16G16B16A16_TYPELESS` | 8 |
| `R32G32B32A32_FLOAT` | `R32G32B32A32_TYPELESS` | 16 |

深度视图使用明确的 typed 格式；对应纹理可以使用同一 typed 格式或相应的 typeless 资源格式：

| 深度视图格式 | 接受的 typeless 纹理格式 | 每 texel／sample 字节数 | 准备视图的只读标志 |
| --- | --- | --- | --- |
| `D16_UNORM` | `R16_TYPELESS` | 2 | 深度 |
| `D24_UNORM_S8_UINT` | `R24G8_TYPELESS` | 4 | 深度和模板 |
| `D32_FLOAT` | `R32_TYPELESS` | 4 | 深度 |
| `D32_FLOAT_S8X24_UINT` | `R32G8X24_TYPELESS` | 8 | 深度和模板 |

两表的格式名省略 `DXGI_FORMAT_` 前缀。资源为 typeless 不代表 DSV 可以使用 typeless。未知 DSV 标志，以及无模板格式上的模板只读标志均拒绝。模块另建 DSV 并设置 `D3D11_DSV_READ_ONLY_DEPTH`；含模板的格式还设置 `D3D11_DSV_READ_ONLY_STENCIL`。它不修改原 DSV，也不创建深度纹理、CPU 图像、shader-resource view 或 resolve 后的深度副本。

## 采集身份与替换

`DepthKey` 包含契约版本、非零附着代次、宿主 `epoch/world/camera/frame/capture` 五项身份、相机配置修订号及选定颜色槽位。对象整个生命周期内，每次成功附着的 generation 都须严格增加。

相机代次不能倒退。同一代次内世界保持不变，相邻成功准备的 frame 和 capture 都严格增加，相机切换也不能绕过。相同代次和相机的配置修订不能降低；复用修订号要求透视、viewport 全字段及相机位置误差限相同。新相机或新代次可从新的正配置修订号开始。view 和位置属于逐次采集姿态，可以随新采集变化。

所属线程每次调用 `prepare` 都先撤销当前附着；替换被拒绝后不恢复旧视图，也不推进成功 generation／相机历史。`stop` 释放当前附着，同时保留历史。恢复须取得有效的新采集和 generation，不能通过停止对象重放旧结果。

`access(key, evidence, camera, context)` 再次核对精确键和完整相机契约，取得当前构建会话快照，检查设备并重新查询当前绑定和 viewport。选定颜色视图必须保持同一 COM 身份。当前深度视图可以是原可写 DSV、本模块的只读 DSV，或相同底层深度资源与布局的其他视图；只允许深度／模板只读标志不同，资源身份、格式、维度和选定 subresource 仍须一致。

所属线程上的 access 失败会撤销当前附着。新的宿主观察、世界／模式变化、构建会话撤销、换绑、viewport 不匹配、设备变化或已报告的设备移除错误都会阻止旧视图继续使用。模块不自动轮询，调用方须在消费前及相关来源变化后执行 `access`。

构建检查使用当前 `BuildSession` 状态，不重新哈希文件，也不独立观察新加载模块。适配器须在帧回调之外更新完整构建测量，测量变化或无法取得时撤销会话；仅凭缓存快照不能检测尚未告知的变化。

## 所有权、界限与操作结果

成功后，模块拥有来源设备、选定颜色视图及新只读 DSV 的引用；DSV 使其深度资源保持存活。绑定／资源查询返回的临时接口在成功和失败路径都释放。context 与 `BuildSession` 仍由调用方负责。

`DepthAccess::view` 是借用指针，在所属线程操作撤销或替换附着、或模块析构后失效。消费方若自行增加 COM 引用或将视图绑定到 context，就承担这些额外生命周期；`stop` 不能释放消费方的引用或解绑其 context。调整尺寸、替换或销毁宿主资源前，消费方须释放自己的绑定和引用，并撤销本模块的附着。

构造函数的来源预算默认 **256 MiB**，须为正数且不超过 **1 GiB**；无效预算抛出 `std::invalid_argument`。`DepthDescription::source_bytes` 计算选定颜色纹理与深度纹理的总量：

```text
source_bytes = width * height * samples * (color_bytes + depth_bytes)
```

格式字节数取自上表。此界限不测量驱动分配粒度、压缩、驻留、共享／context 引用、其他渲染目标或进程 RSS。模块不保留 CPU 像素缓冲；即使没有分配纹理，持有视图也可能延长来源分配的生命周期。

`DepthResult` 区分 `ready`、`inactive`、输入／相机无效、采集过期、不支持的 context／表面、目标缺失／不匹配、viewport 无效、预算超限、设备改变／丢失和 Direct3D API 失败。只有成功附着才提供描述；access 失败不返回可用视图。`HRESULT` 字段独立保留可取得的 API 错误。

构造、使用和析构都须在 context 所属线程完成。`prepare`、`access` 和 `stop` 在接触状态或 Direct3D 对象前检查线程，跨线程访问抛出 `std::logic_error`。析构释放自有引用而不抛异常，也不会把清理派发到其他线程。所属线程还须保证对传入 immediate context 的合法独占访问，包括采用 `D3D11_CREATE_DEVICE_SINGLETHREADED` 的设备；线程身份相同不证明其他宿主组件没有同时操作。

模块不绑定目标、不设置深度／模板状态，也不执行 draw、clear、copy、resolve、map 或等待 GPU 结果。D3D 视图的创建和释放仍有取决于驱动的成本，本契约不保证固定延迟；调用方须确定适用的准备／释放阶段。只读 DSV 不授予渲染权限，也不恢复独立消费方改变的状态。

后续消费方可读取 `DepthDescription::comparison`：普通 Z 为 `LESS_EQUAL`，反向 Z 为 `GREATER_EQUAL`；模块只报告选择，不安装状态。消费方仍须独立验证深度写入策略、模板、剔除、混合及其修改状态的恢复。只读深度保护已有场景深度，本身不解决新提交客体表面之间的顺序或相互遮挡。

## 受控验证与真实宿主验收

[Windows 回归](../../native/tests/d3d11_depth_test.cpp) 注册为 `d3d11-depth` CTest，请求创建 feature level 11.0 的真实 Direct3D 11 WARP 设备。受控场景检查普通／反向 Z 下给定墙面前后的结果，并核对深度数据不变。绘制、staging 和回读均由测试负责，不属于 `D3D11DepthAttachment`。

回归还覆盖支持深度格式的只读标志、来源布局／viewport 拒绝、采集／修订重放、换绑、构建会话撤销、跨线程误用与恢复。COM private-data 生命周期计数器检查停止、析构及替换失败后的引用释放；测试先撤销自己的引用／绑定，并 flush 延迟销毁，再检查最终释放。各版本实际执行结果记录在[开发 PR](https://github.com/ranzevor/HarranCraft/pull/2)；仅构建测试程序不能登记为已执行 WARP 测试。

WARP 使用受控资源运行 Windows D3D runtime。实际硬件设备移除、驱动故障、GPU 内存驻留及耗时须独立验证；构造过期键或传入不同设备不属于硬件 device-loss 实验。

真实消光接入仍须批准完整游戏构建，验证 context 线程和引擎阶段，取得实际相机／pass／深度对应关系，并验收尺寸变化、加载及退出期间的资源释放。当前透视契约不表达 temporal jitter、非对称／偏心投影或无穷远平面；不能丢弃 TAA／jitter 参数以制造矩阵匹配。宿主若采用这些约定、不同的 viewport 深度区间或额外表面格式，须先明确扩展契约并增加回归，再执行附着。模块不注册原生碰撞体、不实现伤害／TNT，也不构成可玩的双运行时版本。

## Direct3D 依据

- [OMGetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omgetrendertargets)：当前 output-merger 视图及交给调用方的引用。
- [RSGetViewports](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rsgetviewports)：活跃 viewport 数量及描述。
- [CreateDepthStencilView](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilview) 与 [DSV 描述](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_depth_stencil_view_desc)：资源／视图创建和 typed 深度格式。
- [只读 DSV 标志](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_dsv_flag)：独立的深度和模板写保护。
- [Texture2D 描述](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_texture2d_desc)：纹理尺寸、mip／数组布局及采样描述。
- [GetDeviceRemovedReason](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-getdeviceremovedreason)：设备故障结果。
- [Direct3D 11 线程约束](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)：immediate context 并发访问要求。
- [OMSetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets)：消费方绑定额外持有的引用。
- [SetPrivateDataInterface](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicechild-setprivatedatainterface) 与 [Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)：隔离测试的生命周期探针与延迟销毁。
- [WARP](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)：通过 Direct3D 实现的 Windows 软件光栅化。
