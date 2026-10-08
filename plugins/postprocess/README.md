# 原生后处理插件

`iris.postprocess` 为第二种静态 RenderPlugin，阶段为 PostProcess，消费借用的场景 HDR 与可选深度、运动、法线，输出到宿主的 Final RenderTarget。原 Bloom/TAA/NPR 合成、色调映射、色彩编码和 LUT 数学保持原样；这里没有新增场景效果。

`IRIS_ENABLE_POSTPROCESS_PLUGIN=OFF` 时不编译此 Target。原生 Raster 管线需要它，因此 Scene/Job 明确拒绝；独立 Enscape 管线不依赖它，仍可运行。不得通过跳过色调映射假装支持原生输出。旧参数仍使用共享 PostProcessSettings，核心只持有 RenderPlugin 抽象；设置不是插件的独立 Scene。

资源声明区分 Input、Output、Transient 与 History；校验同帧读写顺序、多个 writer、纹理类型/尺寸和输入输出别名。只读借用在一次 renderFrame 内有效，GL 资源始终由上下文线程创建/释放。逻辑 Pass 计划用于校验，内部 Blur ping-pong 和 TAA 仍由插件执行；完整自动调度、通用配置与 UI 扩展在 A2-B 后续实现。

部分半分辨率云/光束仍通过旧设置桥传入，由原有生产者负责尺寸/有效性；本轮不声称所有资源都已经过新接口验证。资源格式为语义声明，不是 GPU 驱动反射。插件代码与原生 Shader 继续使用仓库 MIT 许可。

复现：`postprocess-plugin-gpu-acceptance`、`tools/PostProcessPluginAcceptance.ps1`。范围与状态见 [架构路线](../../ARCHITECTURE_ROADMAP.md)。
