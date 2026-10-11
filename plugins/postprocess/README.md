# 原生后处理插件

`iris.postprocess` 为第二种静态 RenderPlugin，阶段为 PostProcess，消费借用的场景 HDR 与可选深度、运动、法线、opaqueDepth、云辐射/深度与光束，输出到宿主的 Final RenderTarget。原 Bloom/TAA/NPR 合成、色调映射、色彩编码和 LUT 数学保持原样；这里没有新增场景效果。

`IRIS_ENABLE_POSTPROCESS_PLUGIN=OFF` 时不编译此 Target。原生 Raster 管线需要它，因此 Scene/Job 明确拒绝；独立 Enscape 管线不依赖它，仍可运行。不得通过跳过色调映射假装支持原生输出。旧参数仍使用共享 PostProcessSettings，核心只持有 RenderPlugin 抽象；设置不是插件的独立 Scene。

资源声明区分 Input、Output、Transient、History 与 Generated；校验同帧读写顺序、多个 writer、纹理类型/尺寸和输入输出别名。只读借用在一次 renderFrame 内有效，GL 资源始终由上下文线程创建/释放。逻辑 Pass 计划用于校验，内部 Blur ping-pong 和 TAA 仍由插件执行；配置与动态参数面板由共享 schema/命令提供；逻辑计划不自动执行私有 Pass。

全/半分辨率云与光束通过统一 frame 绑定声明尺寸、格式与有效性，插件从绑定表消费；启用却缺失、尺寸/格式错误及输入输出别名在写入前拒绝。私有 gradingLut 声明为 Generated。资源格式为语义声明，不是 GPU 驱动反射。插件代码与原生 Shader 继续使用仓库 MIT 许可。

`prepareResources` 提前分配私有 FBO/纹理并复核 Shader 输入，宿主通过完整候选实例的事务重建接口发布；失败保留旧 owner 与输出。Inspector → Plugins 的重建按钮走同一 EditorCommand。

复现：`postprocess-plugin-gpu-acceptance`、`tools/PostProcessPluginAcceptance.ps1`。范围与状态见 [架构路线](../../ARCHITECTURE_ROADMAP.md)。
