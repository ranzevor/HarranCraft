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

[MaterialPublisher](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialPublisher.java) 先在**同一次客户端采集**取得不可变烘焙模型及其全部源精灵，核对最多 16 个独立图集／精灵身份，拒绝缺失、重复或错误来源，然后才分别入队模型和源像素。现有 \`RenderPipe\` 和新的 \`TexturePipe\` 使用不同的调用方自有输出流；纹理通道最多排队**两个完整材质批次**，由守护线程逐批写入，不阻塞客户端采样线程。任一通道失败即撤销两条通路。不同流之间不保证先后顺序；原生消费者必须先有权威模型精灵清单才可使用贴图，未来正式宿主协调器需要显式处理乱序，而不能依赖线程调度。

[MaterialCache](../../include/harrancraft/material_cache.hpp) 只保留一个当前模型采集所需的 CPU 源像素，**全部要求的精灵到齐前绝不对外提供可用材质**。[MaterialReceiver](../../include/harrancraft/material_receiver.hpp) 采用一条最大 16,704 字节的有界记录缓冲处理任意分片；拒绝未知、重复、旧代次或跨世界／相机／帧／采集身份的精灵和格式损坏／截断记录，并在 EOF 不完整、通道终止、撤回或校验失败时撤销全部像素。模型变更立即丢弃旧像素；新会话需要更高代次。这些类型不创建或销毁 GPU 纹理句柄。

Minecraft 客户端的 [HarranCraftClient](../../guest/src/main/java/io/ranzevor/harrancraft/HarranCraftClient.java) 注册 Fabric \`CLIENT_RESOURCES\` 资源重载监听器。[MaterialReloadRegistry](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialReloadRegistry.java) 最多持有四个活动发布器，资源包重载时关闭它们已经拥有的模型／纹理输出端点。客户端调用方仍须检查工作线程真实退出及清理异常；关闭输出不等于宿主已安全销毁图形资源。当前 CI 未启动游戏，不证明实际资源重载回调或 Mixin 已执行。

## 验证与接入边界

验证入口运行 Java 编解码、原生 CTest、完整 336 字节跨语言参考帧逐字节比对、真实 Java→C++ 进程管道往返，以及非法／截断记录拒绝。Fabric 编译客户端 NativeImage 访问器和实际方块源纹理采样的 GameTest 源码；CI **不启动 Minecraft，也不执行 GameTest**。

已实现客户端可选材质发布、原生有界接收和 CPU 像素候选缓存，并通过隔离测试核验模型清单和多精灵完整性交付；但尚未接入消光正式宿主。后续仍需实机资源重载、双通道乱序协调、GPU 上传／释放、动画、染色、动态光照和双相机深度帧关联。仓库不提交游戏贴图及其他资产。
