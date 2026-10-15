# 原生水面插件

iris.water 是可选静态 Surface 扩展，拥有 High/Low 海面网格和 Forward/Deferred 两套水面 Shader。后端通过 opengl.shader-borrow.v1 短期借用 Shader，准备绑定后通过 opengl.indexed-draw.v1 调用插件选择网格并执行索引绘制；后端继续管理附件、GL 状态、时间、运动历史、Camera 与环境/阴影/云反射纹理。插件向自己的已绑定 Shader 写入 WaterSettings 对应的 Uniform；原 Shader 文本、网格和 Uniform 算法保持，WaterWaves 的 CPU 算法仍为原共享源码。

CPU schema 有 19 项绑定参数，仍使用 RendererSettings.water 原字段；旧 Scene、海况 Preset、共享命令与 Job 共用一份值。范围与旧加载器/命令相同；新 typed values 越界拒绝，旧字段仍按原加载规则处理。手动波浪编辑切回 Custom；导入参数块不覆盖旧 Preset。插件启停位于 Plugins，参数位于 Render settings 的 Native water 分组。

仅原生管线且 water.enabled 时要求 iris.water 可用且启用。缺失/停用时，在 Scene/Job/旧水面命令/直接 render 改变状态前拒绝；不悄悄省略水面。普通干燥场景和全屏 ShaderScene 不要求该插件。CPU PT 保持旧共享材质语义。

cmake 选项：IRIS_ENABLE_WATER_PLUGIN，默认 ON；plugins-off preset 为 OFF，水面移除专项保留 PostProcess ON。GPU OFF 时 CPU schema 仍可保存/重开，WaterSurface 和 WaterRenderer 不进入应用。普通干燥场景不自动创建水面程序/网格；停用释放两套程序和两套网格，网格共 1,479,184 bytes。程序的驱动内存不计入此网格估算。重新启用时在上下文线程编译并创建，可能产生一次准备耗时；环境/附件仍在后端，本切片不宣称释放全部水面资源。

网格和两套程序通过 prepareReplacement 先准备候选、验证成功后发布。第二套 Shader 编译失败或取消会销毁候选并保留旧实例；准备过程恢复调用者 GL 绑定。Shader 继续注册在现有全局原子热重载事务中，失败保留旧程序，恢复时一并替换。借用在实例替换/销毁时失效，不能跨帧缓存；后端按实际子调用选择 Shader：颜色始终使用 Forward，Deferred + TAA 先补写运动附件再绘制颜色。viewport resize 不重建固定网格或程序。

迁移验收保留精确图像、Draw/资源量与 CPU P95 原预算。跨运行历史 GPU P95 门槛因未改动 W1 负对照也反复误报而撤弃；GPU 时间继续记录为诊断，既有失败证据与基线保留。这不表示 GPU 性能认证通过，也不影响其他 Shader/性能专项。

复现：water-plugin-gpu-acceptance（真实 GL 网格/程序所有权、编译失败/原子热重载/候选/绑定）；tools/WaterPluginAcceptance.ps1（两窗口实际参数、释放/重启内存与生命周期），tools/WaterJobAcceptance.ps1（GUI/Job 或 WaterOff 拒绝），tools/WaterMigrationAcceptance.ps1（真正旧程序的 before 与新版 after，禁止重拍 before 通过）。本地阶段证据见 docs/a4w1-water-shading-extension.md、docs/a4w2a-water-grid-ownership.md 与 docs/a4w2b-water-shader-ownership.md。

Surface 的 renderFrame 仅适配 Uniform，drawIndexedGeometry 在上下文线程执行一次 draw 并返回计数，未准备或不支持时拒绝。目标、program、纹理、viewport、draw buffers 和 GL 状态由宿主先绑定；插件仅绑定自有 VAO/draw/解除到 VAO 0。Surface 不声明自有逻辑纹理或调度 Pass，MaterialShading 继续只写 Uniform。旧 opengl.indexed-geometry.v1 和 Shader 借用 API 保留兼容。GPU 专项检查 High/Low 实际图元、运动像素、材质/深度隔离与外部状态保持。

W2c-2a 用 WaterFrameInputs 的五个只读字段借用后端已解析的当前/前帧 VP、前帧时间、水面高度和运动有效性；插件写入七项公共逐帧 Uniform。Camera 与当前时间仍来自原 Frame/WaterSettings，VP 保留后端 TAA jitter；两个历史回退、0.2 s 边界和帧末更新仍由后端决定。Frame 构造函数末尾的借用参数默认 nullptr，旧 Settings-only 调用保持；插件不保存指针。后端只读 renderedWaterFrameInputs 暴露实际当帧包供诊断，该包不作为下一帧历史来源。环境/灯光/纹理绑定仍在后端。

W2c-2b 在上述逐帧借用存在时，继续适配颜色程序的 CameraForward、近远面、逆 viewport 和输入包当前 VP 的逆矩阵。按自有 Forward Shader 对象选择，兼容 Deferred 的颜色子调用；运动程序不绑定此组。非正尺寸或非有限逆矩阵在写 Uniform 前拒绝，旧空借用仍保持 Settings-only。共享 Frame/API/服务不扩展；灯光、环境与纹理仍由后端准备。

W2c-2c 通过 Frame 末尾默认空的 WaterLightingInputs 借用统一计算后的灯光方向/颜色、关键光辐照度、漫反射强度、实际环境最大 mip 与 twilight 因子；插件为自有颜色程序适配这六项及原 Settings 的环境强度、分离太阳开关，共八项 Uniform。按实际程序选择，兼容 Deferred 颜色；运动程序不消费，旧 Settings-only / frame-only 调用不覆盖灯光。后端在颜色调用时解析六字段，不复制光照算法到插件；插件不保存输入包。阴影/级联/云 metadata、sampler 和所有纹理绑定继续由后端负责。Shader、保存格式与 19 项 schema 不变。
