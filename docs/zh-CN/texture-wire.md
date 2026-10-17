# 精灵源图像像素传输

[English](../texture-wire.md) · [渲染模型协议](render-wire.md) · [烘焙模型观察](render-model.md)

## 真实 Minecraft 图像来源

客户端专用的 [MinecraftSpriteTextures](../../guest/src/main/java/io/ranzevor/harrancraft/MinecraftSpriteTextures.java) 对照同一客户端已加载方块和固定种子的 BakedModel，与不可变 [RenderFrame](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderFrame.java) 核对四边形、朝向、图集／精灵名称、顶点及数量。[SpriteContentsAccessor](../../guest/src/main/java/io/ranzevor/harrancraft/mixin/SpriteContentsAccessor.java) 是仅客户端启用的 Mixin，读取 SpriteContents 已持有的 NativeImage，拷贝从左到右、从上到下的真实源 ARGB 像素；图集 X/Y 坐标和归一化 U/V 来自 Sprite。不执行 GPU 同步回读，不安装或提交游戏资源。

拒绝客户端世界／相机改变、方块实体、流体、不同的模型四边形、超过 16 种精灵、源图尺寸大于 64×64、与单帧逻辑尺寸不符的图像条带；不截断图像、不以编造的颜色代替。动画图像条带、动态动画帧、着色器、多级纹理选择、生物群系染色、光照和最终显存图集**尚未提取**。已加载源像素不等于最终渲染材质。

## TextureWire 版本 1

[Java](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/TextureWire.java) 与 [C++20](../../include/harrancraft/texture_wire.hpp) 按小端写入 32 位 **0xAARRGGBB** 像素；完全独立于现有 512 字节方块、6,464 字节碰撞及 205,184 字节渲染模型协议，不更改它们。

记录由 320 字节头部及逐像素四字节组成，最大 **16,704 字节**。

| 偏移 | 字节数 | 字段 |
| --- | --- | --- |
| 0 | 4 | 魔数 0x31544348 |
| 4 | 2 | 版本 1 |
| 6 | 2 | SNAPSHOT 1／RETIRE 2 |
| 8 | 4 | 总记录长度 |
| 12 | 4 | 保留零 |
| 16、24、32、40、48 | 各 8 | 正代次、世界、相机、逻辑帧、采集身份 |
| 56、60 | 各 4 | 精灵图集原点 X/Y |
| 64、66 | 各 2 | 宽高（SNAPSHOT 为 1–64） |
| 68 | 4 | 保留零 |
| 72、76、80、84 | 各 4 | 图集 minU/maxU/minV/maxV（32 位浮点） |
| 88、90 | 各 2 | 图集／精灵名称长度 |
| 92 | 4 | 保留零 |
| 96 | 96 | 图集 ASCII，空余补零 |
| 192 | 128 | 精灵 ASCII，空余补零 |
| 320 起 | 每像素 4 | 打包 ARGB 整数 |

五项身份来自调用方，不等于已校准的消光帧。SNAPSHOT 必须有完整像素、非负图集坐标，UV 均有限、位于 [0,1] 且最小值小于最大值。RETIRE 保留图集／精灵标识，其余图像尺寸、位置、UV 和像素全零。双端拒绝非法版本、保留位、文本、尺寸、像素数量、浮点及截断数据；Java 在分配完整缓冲前检查长度上限。

## 多精灵材质归属及资源重载

[MaterialPublisher](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialPublisher.java) 先在**同一次客户端采集**取得不可变烘焙模型及其全部源精灵，核对最多 16 个独立图集／精灵身份，拒绝缺失、重复或错误来源，然后才分别入队模型和源像素。`RenderPipe` 和 `TexturePipe` 使用不同的调用方自有输出流，最多分别排队八条模型记录和两个完整纹理批次。守护线程执行写入和刷新，不阻塞 Minecraft 客户端采样线程。每条输出内部保持 FIFO 顺序，两条输出抵达原生接收方的先后可以不同。

任一传输停止时立即通知另一条传输失效，并从 [MaterialReloadRegistry](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialReloadRegistry.java) 注销发布器，包括异步写入／刷新失败、队列饱和和主动关闭；不等待客户端下一次调用 `publish()` 或 `isOpen()`。通知只操作自有传输和登记表，客户端采样源仍由所属线程访问。每个端点只执行一次取消，I/O 和清理异常分别保留；调用方须保留传输句柄并检查工作线程实际结束。

[HarranCraftClient](../../guest/src/main/java/io/ranzevor/harrancraft/HarranCraftClient.java) 注册 Fabric `CLIENT_RESOURCES` 资源重载监听器。登记表最多持有四个活动发布器，资源重载时使双方自有输出失效。普通 CI 不执行游戏内资源重载回调或 Mixin 加载。

## 跨通道材质协调

[MaterialCoordinator](../../include/harrancraft/material_coordinator.hpp) 在单一原生所属线程上关联两条独立分片的流。`begin(epoch, world, camera, dimension)` 固定会话，模型和纹理的**会话代次、世界、相机、逻辑帧、采集身份**必须逐项相同。模型采集号必须连续，逻辑帧严格递增；同一次采集的精灵可按任意顺序到达，后续纹理批次的采集号和逻辑帧必须递增。空模型不产生纹理，因此纹理采集号的跳跃须结合模型清单判断；较早的已知清单仍缺精灵时抵达新纹理批次，会将会话判为不完整。

先于模型到达的纹理按采集身份隔离暂存，只有权威模型要求的全部精灵到齐后，`find()` 才提供完整材质。新模型、空快照或模型 RETIRE 抵达时，立即撤销同位置旧材质。已被替换但仍待接收的采集释放模型和像素，仅保留有界精灵清单及已见身份，供验证后续在途纹理；旧采集补齐后也不会恢复旧模型。

| 边界 | 上限 |
| --- | --- |
| 尚未完成的采集关联 | 16 |
| 当前模型位置，包含尚未补齐材质的新模型 | 16 |
| 保留的模型／纹理记录身份，包含在途数据校验记录 | 256 |
| 保留数据计量预算 | 4 MiB |
| 固定模型及纹理解析缓冲 | 205,184 + 16,704 = 221,888 字节，在保留数据预算外单列 |
| 组装超时 | 默认 1 秒，可配置为大于零且不超过 5 秒 |

保留数据预算计入模型头部／四边形、完整纹理记录及有界精灵清单／已见身份的存储量。STL 容器及分配器元数据受记录、精灵和采集数量约束，但不按该预算测量；**4 MiB 不是进程 RSS 上限**。纹理采集号最多领先已接收模型流 16 次。超过任一准入边界都会清空整个关联会话。

组装期限使用 `steady_clock`，从不完整采集的首个片段或首条关联记录开始计算；追加片段、另一通道抵达和其他采集的流量均不延长期限。所属线程须定期调用 `poll()`，没有输入时也必须调用；`feed_models()`、`feed_textures()` 和 `find()` 同样检查期限。已完整材质没有独立 TTL，但其他未完成采集超时仍会使整个会话及这些材质失效。传入的单调时钟倒退会被拒绝。

未知或重复精灵、旧身份或身份不符、记录损坏及容量／超时失败，均清空双方解析缓冲、待关联采集和可用材质。匹配清单的纹理 RETIRE 撤销整个关联会话；先于模型抵达的 RETIRE 需暂存至清单可供核验。任一流 EOF 都结束整个会话：该流仍有半条记录时判为截断，其他流仍有片段或未完成关联时判为不完整，否则正常结束。通过 `fail(channel)` 报告 I/O 失败或显式 `stop()` 同样清空自有 CPU 状态；恢复必须使用严格更高的代次。协调器不持有 I/O 端点或游戏资源，端点关闭由调用方负责，已结束会话不能继续使用。

[MaterialCache](../../include/harrancraft/material_cache.hpp) 和 [MaterialReceiver](../../include/harrancraft/material_receiver.hpp) 继续供能保证先提供单一权威模型、再提供其纹理的调用方使用；它们与协调器复用精灵清单校验，并保留严格单模型、有界增量接收契约。独立异步模型／纹理输出使用协调器关联。

## 验证与接入边界

`python tools/test_bridge.py` 注册 Java 纹理编解码及材质发布回归、原生 CTest、完整 336 字节跨语言参考帧比较和真实 Java→C++ 进程交换。`material-coordinator` CTest 覆盖保持各流 FIFO 的跨流交错、分片、旧采集替换、清单错误、准入边界、绝对期限、EOF 和新代次恢复。`MaterialPipelineTests` 检查客户端所属线程不再调用发布器时，异步故障仍能取消双方输出。

[MaterialCoordinatorProcessTests](../../guest/src/test/java/io/ranzevor/harrancraft/bridge/MaterialCoordinatorProcessTests.java) 通过两条真实 OS 管道驱动 [hc-material-coordinator-peer](../../native/tests/material_coordinator_peer.cpp)：模型走进程 stdin，纹理在 Windows 使用私有命名管道，在 POSIX 使用私有 FIFO。测试完整比较返回的模型与像素字节，使用实际异步发布器强制纹理先到，并检查缺失／重复／错误身份、超时、通道 EOF、撤销以及进程／端点清理。已有完整材质可用后，受控阻塞双方写线程以填满纹理队列，验证饱和触发双方关闭及原生材质撤销；恢复场景使用更高代次建立新的发布器、进程和端点，拒绝旧代次，并核对全部登记槽位重新可用。原有按序 `MaterialProcessTests` 继续独立验证单模型接收器。

固定 Fabric 构建编译客户端 NativeImage 访问器、材质发布器、重载监听器和 GameTest 源码；普通 CI **不执行 Minecraft、GameTest 或 Mixin 注入**。当前 CPU 协调接口尚无正式消光宿主适配。后续接入仍包括实际客户端重载、游戏内跨运行时传输、GPU 上传／释放、动画、生物群系染色、动态光照及相机／深度对应。仓库不提交游戏贴图及其他资产。
