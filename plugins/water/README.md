# A4-W1：原生水面着色适配插件

iris.water 是可选静态 MaterialShading 扩展，借用后端已绑定的水面 Shader，只写 WaterSettings 对应的 Uniform，不绘制、不保留借用、不分配网格、Shader、纹理/FBO 或历史。Forward/Deferred 共用适配；WaterWaves 的 CPU 算法仍为原共享源码。后端继续管理时间、运动历史、Camera、环境/阴影/云反射纹理、海面网格和绘制。

CPU schema 有 19 项绑定参数，仍使用 RendererSettings.water 原字段；旧 Scene、海况 Preset、共享命令与 Job 共用一份值。范围与旧加载器/命令相同；新 typed values 越界拒绝，旧字段仍按原加载规则处理。手动波浪编辑切回 Custom；导入参数块不覆盖旧 Preset。插件启停位于 Plugins，参数位于 Render settings 的 Native water 分组。

仅原生管线且 water.enabled 时要求 iris.water 可用且启用。缺失/停用时，在 Scene/Job/旧水面命令/直接 render 改变状态前拒绝；不悄悄省略水面。普通干燥场景和全屏 ShaderScene 不要求该插件。CPU PT 保持旧共享材质语义。

cmake 选项：IRIS_ENABLE_WATER_PLUGIN，默认 ON；plugins-off preset 为 OFF，水面移除专项保留 PostProcess ON。GPU OFF 时 CPU schema 仍可保存/重开，后端尚保留原有水面资源，因此本切片不宣称移除插件即可释放全部水面显存。

复现：tools/WaterPluginAcceptance.ps1（两窗口实际参数/生命周期），tools/WaterJobAcceptance.ps1（GUI/Job 或 WaterOff 拒绝），tools/WaterMigrationAcceptance.ps1（真正旧程序的 before 与新版 after，禁止重拍 before 通过）。本地验收证据在 docs/a4w1-water-shading-extension.md 与 .local/verification/a4w1。
