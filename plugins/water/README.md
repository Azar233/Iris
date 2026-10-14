# 原生水面插件

iris.water 是可选静态 MaterialShading 扩展，借用后端已绑定的水面 Shader，写入 WaterSettings 对应的 Uniform。A4-W2a 将固定 High/Low 海面网格的 VAO/VBO/EBO 所有权迁入插件；后端通过 opengl.indexed-geometry.v1 服务短期借用 uint32 索引三角形网格，继续管理 Shader、绘制、附件、时间、运动历史、Camera 与环境/阴影/云反射纹理。Forward/Deferred 共用原网格和 Uniform 算法；WaterWaves 的 CPU 算法仍为原共享源码。

CPU schema 有 19 项绑定参数，仍使用 RendererSettings.water 原字段；旧 Scene、海况 Preset、共享命令与 Job 共用一份值。范围与旧加载器/命令相同；新 typed values 越界拒绝，旧字段仍按原加载规则处理。手动波浪编辑切回 Custom；导入参数块不覆盖旧 Preset。插件启停位于 Plugins，参数位于 Render settings 的 Native water 分组。

仅原生管线且 water.enabled 时要求 iris.water 可用且启用。缺失/停用时，在 Scene/Job/旧水面命令/直接 render 改变状态前拒绝；不悄悄省略水面。普通干燥场景和全屏 ShaderScene 不要求该插件。CPU PT 保持旧共享材质语义。

cmake 选项：IRIS_ENABLE_WATER_PLUGIN，默认 ON；plugins-off preset 为 OFF，水面移除专项保留 PostProcess ON。GPU OFF 时 CPU schema 仍可保存/重开，WaterSurface 实现不进入应用。普通干燥场景不创建网格；停用释放两套网格共 1,479,184 bytes，重新启用时在上下文线程创建。Shader 仍由后端拥有，本切片不宣称释放全部水面资源。

网格通过 prepareReplacement 先准备候选、验证成功后发布。失败或取消会销毁候选并保留旧实例；准备过程恢复调用者 GL 绑定。借用在实例替换/销毁时失效，不能跨帧缓存。固定网格不随 viewport resize 重建。

复现：water-plugin-gpu-acceptance（真实 GL 所有权/候选/绑定）；tools/WaterPluginAcceptance.ps1（两窗口实际参数、释放/重启内存与生命周期），tools/WaterJobAcceptance.ps1（GUI/Job 或 WaterOff 拒绝），tools/WaterMigrationAcceptance.ps1（真正旧程序的 before 与新版 after，禁止重拍 before 通过）。本地阶段证据见 docs/a4w1-water-shading-extension.md 与 docs/a4w2a-water-grid-ownership.md。
