# Minecraft 烘焙模型观察

[English](../render-model.md) · [架构](architecture.md) · [宿主观察](host-observation.md)

## 真实模型来源

仅用于客户端的 [MinecraftRenderModels](../../guest/src/main/java/io/ranzevor/harrancraft/MinecraftRenderModels.java) 在 Minecraft 客户端线程，通过 BlockRenderManager 读取固定 Minecraft 1.21.4 的真实 [BakedModel](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/client/render/model/BakedModel.html)。方块状态来自已加载的真实客户端世界，模型随机种子由位置及状态确定。分别取得无面裁剪与六个方向的 [BakedQuad](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/client/render/model/BakedQuad.html)；不会把物理碰撞盒冒充渲染网格。

每个四边形保留裁剪面、模型面、图集及精灵标识、染色索引、阴影与自发光元数据；四个顶点保留方块局部浮点位置、图集 UV、压缩颜色、亮度和法线。固定顶点格式必须严格符合每四边形四顶点、每顶点八个 32 位整数；未知布局或非有限数值拒绝解析，不作猜测。每次最多 512 个四边形；输出为不可变快照，不传递 Minecraft 自有内存引用。

输入限定为私有嵌入式单人世界中已加载的普通方块模型。空气等不可见方块返回空模型快照；流体方块、方块实体渲染、非模型渲染类型、未加载或越界位置、非法模型数据均明确拒绝，不以简单方块代替。当前未传输图集纹理像素、连接纹理、最终着色器效果、动态实体、流体和实际渲染光照。

## 相机及生命周期对应

[RenderFrame](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderFrame.java) 携带正会话代次、世界、相机、逻辑帧、采集身份，维度及位置、规范方块状态、不可变模型数据，并读取 Minecraft 相机的真实位置、偏航及俯仰角。数字身份**由调用方分配**，不能据此声称取得消光的相机身份、GPU 帧或原生场景同步。

[RenderCaptureSession](../../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderCaptureSession.java) 限制客户端线程归属、递增逻辑帧／采集编号及维度一致性；客户端适配器绑定实际 ClientWorld、集成服务器和 Camera 对象。世界、相机或模式变化，以及读取失败，都会使本次会话失效；恢复须重新建立会话并分配新身份。采样不执行 GPU 同步回读或阻塞 IPC。

H1 宿主相机门禁另外要求验证真实的安全只读阶段。未来 H2-B／H3 合成器仍须实测双方坐标变换、投影矩阵、帧时序和消光深度资源。仅靠数值身份一致、偏航俯仰或独立模型四边形，不能认定双方画面已经正确对应。

## 验证范围

不依赖 Minecraft 的纯 Java RenderFrameTests 实际运行八整数顶点解码、容量及浮点边界、不可变数据、代次递增、身份不匹配和跨线程拒绝。固定 Fabric 构建编译客户端模型适配器及客户端 GameTest 源码，后者在真实游戏执行时采样放置方块的烘焙模型并核验空气模型为空。CI 不启动 Minecraft，不接受 EULA，也不执行 GameTest。独立的[渲染记录 v1 编解码器与可选发布器](render-wire.md)在 Java／C++ 之间保留烘焙四边形、图集／精灵标识及相机身份，经过隔离进程测试，并具有有界客户端异步输出。尚无正式渲染网格生产通道、图集像素、消光几何提交或带深度的原生合成。
