# Iris 插件架构验证路线

> 2026-10-07；用户决定暂停场景渲染/画质研发，优先完善通用引擎架构。
> 当前渲染成果的 GitHub 检查点：`63039f207e5d6c8545326a1cfe5c6ac952b2e49d`，分支 `codex/m1-module-scene-persistence`。
> 所有架构实验位于 `codex/plugin-architecture-validation`，不推送/合并到 `main`；验证失败可继续使用检查点。

## 范围与原则

目标是支持用户扩展的渲染引擎与编辑器。核心保留共享数据合同、任务与资源生命周期；具体后端、管线、渲染效果、模拟和编辑器能力逐步成为可组合插件。已有 Scene/Camera/ModelData/Job、CPU Snapshot、事务加载、取消与历史失效继续兼容。

暂停云海新画质、天气/天文、场景作品扩展及专门的新展示包。保留现有实现、示例和测试作为迁移对照，不立即删除原生云海。既有“最新云海发布 M2-E”后移为维护任务，不再阻塞架构验证。为架构验收运行既有场景不算继续场景研发。

首版为静态 C++ 插件和明确注册，不做 DLL ABI/热卸载、市场、节点图、大一统 RHI 或无限扩展的 Render Graph。Shader 热编译与原生插件卸载是不同合同。OpenGL 插件仍由上下文线程创建、执行和销毁，后台只生成 CPU 数据。

## 执行顺序

| 阶段 | 具体工作 | 进入下一项的门槛 |
| --- | --- | --- |
| A0 检查点与隔离 | 保存/推送当前成果，实验分支从明确 SHA 起步 | 远端检查点 SHA 一致，main 不变 |
| A1 最小宿主与真实迁移 | 版本/ID/所需服务/工厂注册、启停与诊断；Enscape 四 Pass 作为首个可选编译插件 | 两编译器构建/测试，真实 GPU 画面与检查点一致，关闭插件能运行普通场景，缺失能力拒绝请求 |
| A2 核心扩展合同 | 输入输出资源、Pass 顺序、历史/resize/释放、配置与参数、事务能力校验、报告与编辑器扩展 | 第二个不同能力按同一合同接入；移除能力不污染 Scene/任务状态；不靠插件读取宿主内部私有状态 |
| A3 用户 Shader | 全屏单/多 Pass ShaderScene，以及模型材质 Shader；版本化包、公开参数、关联文件与错误定位 | 新场景/材质只新增包与源码；保存重开、编译失败恢复、固定输出与任务合同通过 |
| A4 内置能力迁移 | 分批迁移 NPR、云水、PBR 等；CPU PT 与后端逐步独立，保持共享材质/场景语义 | 每次迁移有同输入旧/新图像、性能、禁用构建和支持矩阵；不一次重写所有渲染器 |
| P1 最小 CPU 物理 | PhysX 求解独立于 GPU 后端；球/箱/地面、固定步长、回退重算和姿态缓存 | GUI/Batch 同帧姿态、不同显示 FPS、取消/缓存与发布验收通过 |
| M3 Vulkan 与 GPU PT | Vulkan 后端插件、Raster、Ray Query、基础 GPU PT、累积/降噪 | 消费共享数据；逐切片正确性/生命周期/性能验收 |

A1 是可行性实验，不代表通用插件平台完成。首次迁移不修改 Shader 算法，保留 `.myscene` 旧 Enscape 开关/参数，仍借用 RendererSettings；跨插件配置、通用服务依赖图、动态 UI 与材质扩展在 A2/A3 才实现。只有 A2 证明第二种能力可接入后，才扩展全面迁移。

2026-10-13 A3 全屏 ShaderScene 子集已由 S1-A～F 验收并完成 Windows Runtime 交付：v1/v2/v3、mainImage、多 Pass/反馈/纹理、统一时间/Camera/鼠标输入、文件与参数工作台、完整包事务重载、schema 4 序列 Job 和输入报告均已接通。新增随包 `10_mouse_shader_sequence.renderjob`，与 08/09 共用同一序列合同；离线鼠标固定零输入。S1-E 的 MSVC/GCC 各 37/37 与真实 GPU 专项作为渲染合同证据；S1-F 补独立目录验收，并修复 Queue 默认 Job 错用源码根的问题。模型材质 Shader 的 A3-M1 最小纵向切片已完成：Entity 绑定、Forward 静态不透明模型、参数、Scene 事务及原生 Raster Job。两编译器 CPU 各 37/37，最终报告合同各 1/1，真实 GPU/双窗口/两帧 Job/63 图及 Label Capture 通过；其他路径明确拒绝。A3-M2 接通模型包的关联文件、include 原行诊断与完整包重编译。选择模型包只浏览，显式绑定到选中模型；未选择模型时禁用。独立 Runtime 模型 Job 的输入身份、资源与许可通过专项检查；当前支持范围保持全屏 ShaderScene v1/v2/v3 与模型 ShaderMaterial v1。交付 ZIP 与远端状态按本次完成记录核对；A4-N1 的 NPR 参数/着色绑定已验收，A4-N2 屏幕效果设置适配已验收，下一入口为 A4 后续迁移分包调查，随后按 A4 → P1 → M3 推进。当前支持范围与模板见 [ShaderScene 说明](assets/shader-scenes/README.md)与[模型材质说明](assets/shader-materials/checker/README.md)。

2026-10-11 A3 首个单 Image 切片已验收：严格 `.shaderscene` v1、包内有界 float 初始值、项目 Camera/时间/分辨率、单 Pass display 输出和 Orbit 模板，通过现有打开/拖入/Scene 相对引用/PNG 捕获运行。双编译器最终 CTest 各 33/33、真实 GL 失败保留/恢复/resize/discard 清零、双尺寸应用往返和重复输出、既有十套图像/Stylized/PT 与带 ShaderScene Image Label 的 RDC 通过；未改基线。后续多 Pass、反馈、mainImage 和纹理见下条；A3 父项仍未完成，运行参数覆盖 UI、文件工作台、Job 和模型材质 Shader 按后续切片推进，当前 Job 明确拒绝且无输出。

2026-10-11 S1-A2a 已推进 v2 有界多 Pass、二维 Pass 通道、RGBA16F 全/半尺寸与整帧双缓冲反馈，支持拓扑排序、周期拒绝、暂停重绘和历史清零；自制 Feedback 包不需要专用 C++ 场景代码。双编译器 33/33、真实 GPU 通道/采样/失败保留/24 帧重复与双窗口工作台通过。S1-A2b1 增加 v2 `mainImage` 包装，所有 Pass 共用声明入口，复用初始编译、include 跟踪和原子重载；v1 合同不变。S1-A2b2 增加 v3 有界 PNG/raw 2D/3D 与固定 Seed hash-v1、sRGB/Mip/通道采样、内容身份和完整资源事务；示例及约定见 assets/shader-scenes/textures。S1-A2b3 将 Enscape 01/03/noiseReduction 迁入通用四 Pass 包，v3 CPU schema source 保留共享旧参数，显式兼容引用不改变旧输入路径；同输入图像/24 帧与成本门已通过。私有纹理预算与旧路径相等；参数变化清历史。S1-B1 已完成 ShaderScenes/Shaders 分类与声明依赖树，按 Refresh 更新只读快照，错误包不改变运行实例。S1-B2a 增加源码/目录打开与完整包事务编译，失败保留运行实例、成功清历史。S1-B2b 增加嵌套 include/父文件原始行、独立 mainImage adapter 来源与 Pass 诊断，工作台支持错误选择和源码上下文；双编译器各 37/37 与真实 GPU/双窗口/受影响固定图验收通过。S1-B2c 接通通用 float/bool 覆盖、共享命令、运行 uniform 与 Scene 保存重开，兼容包继续使用原 CPU schema；Inspector 展示已提交 Pass/Channel 和帧/历史，并支持默认值恢复与清历史。S1-B3 在成功打开后注册独立包的声明资源根，支持会话内保留、依赖去重与 Missing/恢复，包内 include 统一 UTF-8 路径。S1-B4 补齐 Manifest/GLSL/include/PNG/raw 的完整包自动事务重载，失败保留配置/反馈，新增缺失输入补齐后自动恢复，成功清历史；包内程序不参加原生逐程序事务。S1-B5 通过 Scene 可选 history v1 保存参数/相机重置开关与最大正向时间步；默认与旧包相同，策略变更、resize、包替换、倒退仍强制清零，GUI/运行层共享校验。A3 后续按 S1-C Timeline/任务及材质 Shader 合同推进，S1-A2 的交付状态见本地路线图。

PBR/NPR 是着色或管线能力，光追是执行技术，可以组合；后端、管线和效果不混成互斥枚举。Material/Light/Camera 数据合同保持共享，CPU/GPU 支持差异明确诊断。现有 SceneModule 只控制运行场景/模拟，不直接扩充为持有 GL 对象的渲染插件。

## A2 分包与当前范围（2026-10-08）

A1 的 GitHub CI 已成功，当前实现基准为 `38b1995`。A2 分成两个独立验收工作包，父项在全部完成前保持未完成：

- **A2-A（2026-10-08 本地验收完成）**：插件 API v2、Input/Output/Transient/History 的有序逻辑 Pass 合同、借用纹理类型/尺寸/别名校验；共享后处理管线已迁到第二个可选 Target。双编译器 ON/OFF、最终 28/28、真实 GPU 资源消费/拒绝/历史/resize/重建、固定图 SHA 相同、十套渲染/Stylized/CPU PT 回归通过，默认构建恢复 ON。无 Shader 算法或基线修改。
- **A2-B1（2026-10-08，GUI 启停切片）**：Inspector 的 Plugins 页统一显示静态插件目录、编译可用性和当前管线用途；启停通过共享 EditorCommand 校验，随 Scene 保存。当前管线必需项锁定，切换到停用能力或加载无效配置时保留当前状态；停用未使用能力在下次 Raster 帧的上下文线程释放实例，重新启用按需创建。CTest 新增真实 ImGui 输入注入与配置事务测试；GPU 命令/保存重开/释放重建由 tools/PluginGuiAcceptance.ps1 验证。
- **A2-B2（2026-10-11 本地验收完成）**：通用参数持久化与动态参数面板、Shader 编译失败后的事务式资源替换、完整输入资源声明。启停配置不等于通用参数平台；复用已有 ParameterRegistry/EditorCommand，避免另建 Scene 或参数真相来源。
  - **A2-B2a（2026-10-10，验收完成）**：ParameterRegistry 成为模块与渲染插件共享 target；descriptor 声明参数元信息与绑定，Inspector → Plugins 自动生成面板。两个现有插件共 18 个参数，新 Scene 参数块 version=1，旧字段兼容读取；绑定修改同一 RendererSettings，未知/重复/类型/范围错误事务拒绝。双编译器 ON 全构建各 32/32、两插件 OFF 参数/控件测试、真实 GPU 贡献/旧字段等价/保存重开、双尺寸与 260 px 长标签、真实 ImGui Bool/Float 输入、Raster/CPU Job 无产物拒绝、十套图像回归与 CPU PT 回归通过，既有固定图 SHA 不变。源码与共享进度在当前实验分支一并交付，新提交 CI 状态单独核对。
  - **A2-B2b1（2026-10-10，验收完成）**：同一上下文线程中变更/待处理 Shader Program 整批准备与发布，编译或链接失败保留所有旧程序与历史；修复 include、创建缺失依赖或释放阻塞 owner 后可恢复。构造/候选失败采用 RAII 清理；Renderer 面板显示日志并支持真实命令重试。双编译器全构建、CTest 各 32/32、真实 GL 编译/链接失败与恢复、跨 Enscape/Postprocess 旧图保留与提交贡献、双尺寸重试 UI、GPU smoke、十套既有图像回归及两插件 GPU 生命周期专项通过。未改 Shader 算法、阈值或基线。全局批次可能被闲置插件错误阻塞；这不是整个插件实例/纹理的事务替换，未新增性能测量或发布包。
  - **A2-B2b2（2026-10-11，验收完成）**：支持未绑定参数按插件 ID/schemaVersion 独立存储，CPU schema 目录显式注册并供 Scene codec 使用；新插件无需为每个参数增加 RendererSettings 字段。共享命令、动态控件与 frame 读取沿用现有路径，绑定值仍由原字段拥有。双编译器全构建/CTest 各 32/32、两内置插件 OFF 参数与控件 2/2、三种构建的真实 GL 值消费/重开/重建/拒绝/双尺寸与 260 px 面板、旧参数 GUI/GPU/Raster+CPU Job 验收、GPU smoke/十套图像回归及两插件生命周期专项通过。测试插件不加入正式目录，没有 Shader 算法或基线修改。
  - **A2 收口（2026-10-11，本地验收完成）**：补 Asset 路径/过滤/相对保存与显式 CPU schema 迁移；全/半分辨率云/光束进入统一 frame 校验并实际消费，私有噪声与 LUT 用 Generated 声明；完整候选实例提前准备 Shader/FBO/纹理，检查资源身份和取消/过期后才替换，失败保留旧参数/owner/画面/历史，提供真实重建命令。三种构建（MSVC/GCC ON、MSVC 两插件 OFF）全构建/CTest 各 32/32、真实资源/Shader/双尺寸控件/两插件生命周期、OFF 双管线无产物拒绝及 CPU Job、十套图像/Stylized/CPU PT/Module/云/GUI+Batch 对照通过；真实 RenderDoc 1.46 帧与标签检查通过，原生固定图 SHA 不变。当前原生 CPU P50/P95 1.7346/2.5829 ms、GPU 0.660480/1.193984 ms，GPU P95 在原门槛 1.597728 ms 内；CPU 高于历史 A2-A，未做前一 HEAD 的同轮隔离对照，不归因或宣称零开销。交付提交及 CI 单独核对。
  - **完成边界**：A2-A/B/B2 合同在静态 OpenGL 范围内收口；现有 18 个字段绑定作为兼容适配保留，没有两份值。自动物理 Render Graph、DLL ABI/热卸载、用户包发现和跨后端 RHI 不属于 A2；A3 已开始单 Image 资源包切片，A4 尚未开始。

A2-A 只验证有序逻辑计划，不是自动执行的通用 Render Graph；私有 Bloom ping-pong/TAA 等仍由插件执行，半分辨率云与光束的资源校验仍沿用原有生产者和旧设置桥。HDR/深度/运动/法线等帧大小绑定由新接口检查并消费。关闭后处理插件意味着原生 Raster 管线不支持，需明确拒绝；Enscape 全屏场景仍可运行，不悄悄跳过色调映射。

## A1 真实验证任务卡

- **范围**：把现有 Enscape 实现移到独立插件 Target；宿主通过抽象渲染接口与注册工厂消费，不再直接构造具体类。原生云海/NPR/PBR 暂不迁移，不改图像基线。
- **兼容**：旧 01/03 开关、参数、时间、历史与 GPU 计时保持；插件未构建时，GUI 场景加载事务拒绝，Raster Job 在输出前失败，UI 不允许开启缺失能力。
- **输入**：01 固定 1280×720 / 1.25 s / 64 帧预热，03 与现有重复序列；普通共享场景覆盖插件关闭。
- **验证**：注册冲突/API/缺服务/工厂失败测试；MSVC/GCC Release 构建与 CTest；真实 GPU smoke、GLSL 固定与重复输出、resize/历史/释放检查及受影响回归；ON/OFF 构建与拒绝无产物。
- **性能**：同机位 64 帧预热/240 帧测量，记录 CPU/GPU P50/P95；无算法变化的第一轮以画面一致为首要门槛，预设 GPU P95 增幅不超过 max(10%, 0.5 ms)，超出先复测诊断，不改门槛。
- **失败决策**：不能分离实际实现、禁用后核心不可用、输出漂移无法解释或资源生命周期失败时，A1 不通过；停止扩大接口，保留实验分支并回到检查点研究原因。
- **交付**：实验代码、复现工具及结果在实验分支提交/推送；不自动合并 main。A1 不生成新场景发布包，不把当前旧 ZIP 当新架构产物。

## 状态

- A0：当前成果已提交并推送，检查点 `63039f2`；远端 main 仍为 `b135c6e4e6f94ae8cf77d68491024199b37ff622`。
- A1：最小静态插件可行性验证及 GitHub CI 通过，提交 38b1995。
- A2-A：后处理第二插件与资源/Pass 合同本地验收及 GitHub CI 通过，提交 71c866a。A2-B1：GUI 启停与 Scene 配置切片本地验收及 GitHub CI 通过，提交 4ba850c；最终双编译器 30/30、真实 ImGui 输入、应用命令/GPU 释放重建/保存重开、Job 无产物拒绝、GPU smoke 与十套既有图像回归通过。MSVC 默认 NPR 迁移前后图 SHA 相同，跨编译器横向对比不宣称逐位一致；没有改 Shader 或基线。这是此前 A2-B1 的验收记录；当前 A2 已本地收口，交付 CI 另核对。A3 已推进单/多 Pass ShaderScene、反馈、文件/参数工作台与完整包事务重载；可保存的历史策略已落地；通用任务输出及模型材质 Shader 已在 S1-E 与 A3-M1/M2 接通。A4-N1 已开始并完成首个绑定切片；P1/M3 尚未开始。
- 本文件用于 GitHub 上的共享计划；本地详细 `todolist.md`/`docs` 继续受 Git ignore 管理，没有强制加入历史资料。

## 附加工作与进度同步（2026-10-10）

此次整理已有 DBG-1 与 L1 工作，不扩展 A2 的范围。两项功能共用 Application 与 Renderer 接入代码；功能提交为 `1cd8619`，进度记录单独提交到当前实验分支。对应新提交的远端 CI 状态另查，不继承旧提交的成功状态。

| 工作包 | 新增能力 | 已有验收证据与范围 |
| --- | --- | --- |
| DBG-1 | Inspector → Buffers 的五个 G-buffer 通道、独立 SSAO 预览和真实附件 PNG 导出 | GPU 状态恢复、最终图像字节一致、resize/释放/重建、真实抽屉鼠标输入与六种 UI 状态通过；Deferred / screen-space 回归通过 |
| L1 | Point / Spot Scene 实体，层级创建、选择、变换、复制、删除、颜色/强度/范围/锥角与启停 | 保存重开、旧数组兼容、Module/缓存与 CPU Snapshot 贯通；双编译器各 31/31，Forward / Deferred / CPU 内部数组等价与开关贡献、GPU smoke 和相关图像回归通过 |

DBG-1 输出为 8-bit 可视化 PNG，Depth 是设备深度对比度图；不提供浮点附件、ShadowMap/BloomMap 专用预览。L1 实时局部灯光尚无阴影，无体积散射、Directional 实体或视口拖动 TRS；CPU 使用既有遮挡采样。两项均未做新增性能基准，也未生成新的发布 ZIP。

复现入口：

```powershell
cmake --build build-ci-msvc --config Release --parallel 6
ctest --test-dir build-ci-msvc -C Release --output-on-failure
cmake --build build-mingw --parallel 6
ctest --test-dir build-mingw --output-on-failure
cmake --build build-ci-msvc --config Release --target gpu-smoke gpu-buffer-preview-acceptance buffer-preview-capture light-entity-acceptance
```

两套 CTest 应串行执行：既有 Render Job 测试共用固定临时目录，并行跨构建执行会互相覆盖产物。GPU 验收串行运行；构建复制资产时也不要同时运行读取这些资产的测试。详细图像与历史日志保留在本地 `docs/buffer-preview-inspector.md`、`docs/light-entities.md` 和 `output/`；这些忽略路径不加入提交。

2026-10-10 整理复验：MSVC / GCC Release 全目标构建成功；串行 CTest 各 31/31，退出码 0，耗时 46.55 s / 36.47 s。最初跨构建并行运行两套 CTest 时，`render-job-runtime` 因共享临时产物失败；串行重跑通过，未改断言或图像基线。

最终 `gpu-smoke`、`gpu-buffer-preview-acceptance`、`buffer-preview-capture`、`light-entity-acceptance` 全部退出码 0。真实抽屉/Add Light 输入、六通道与禁用 SSAO 无旧图、最终图 SHA 不变、保存重开以及 Forward / Deferred / CPU 的旧数组等价与启停贡献均通过。GPU 首次启动因与测试并行读取时资产复制失败，串行重跑通过；未修改源码以绕过检查。本次未重新运行图像回归套件，沿用 2026-10-09 对同一功能代码的专项记录；未做性能基准或新 ZIP。

后续主线仍为 A2-B2 通用参数 schema/保存/动态面板、事务资源替换与剩余输入资源合同。灯光后续拆为 L1-B 编辑增强、L2 实时局部阴影、L3 可选体积光；L3 依赖 A2-B2 / L2，均未开始，不因本次推送勾选。

## 既有架构迁移证据

迁移对照为 01 / 1280×720 / 1.25 s / 64 帧预热，检查点与插件 PNG SHA-256 均为 `CF455627E64FE7C2C04B963B01F02538B341F62546118894C8AE7036B5A1EBF2`。最终 240 帧测量 GPU P95 8.299520→6.778880 ms，满足预设上限 9.129472 ms；时钟/温度会影响单次值，不宣称插件化加速。无 Shader 算法或基线变化，无新 ZIP。

复现入口：A1 的 `tools/RenderPluginAcceptance.ps1`（需先捕获检查点 before.png / before.json）、`render-plugin-gpu-acceptance`、`tools/GlslOceanAcceptance.ps1`；A2-A 的 `postprocess-plugin-gpu-acceptance`、`tools/PostProcessPluginAcceptance.ps1`、CTest `render-plugin-contract`。注册表不是完整服务依赖图，逻辑 Pass 校验不是自动资源调度，工厂失败测试不等于 GUI 资源替换恢复。

A2-A 同输入 1280×720 / 4× MSAA / 1.25 s / 64 帧预热 / 240 帧测量：CPU P50/P95 0.6323/1.1338→0.6991/1.1926 ms，GPU P50/P95 0.571392/0.785408→0.595968/1.097728 ms；Draw Call 28、报告 renderMemoryBytes 400069816 字节不变。后处理 CPU Pass 中位耗时约增加 0.0079 ms；整帧采样存在波动，不宣称零开销或加速。既有 NPR 场景迁移前后 PNG SHA 同为 `863A11FC841A9789316078A5DBED1613F606D0E970FC269881516EE89166A5A2`。当前目标继续是架构合同，未开发新场景效果。

## A4 首个迁移工作包（冻结范围）

A4-N1 先迁移 NPR 的公开参数与 Forward/Deferred 着色绑定边界。开始时调查 Renderer.cpp 的 bindStylizedSettings、现有共享参数 schema、basic/deferred Shader 与 PostProcess 调用，固定同输入旧/新对照，再选择满足现有静态插件合同的最小适配。保持 Scene/Material、旧预设和参数唯一值来源，不同时迁移云水/PBR，不修改 NPR 画质算法，不新增通用 Render Graph、DLL ABI 或后端 RHI。轮廓/Dither/Fog 的现有 PostProcess 所有权与依赖先确认，必要时拆成独立后续切片。

验收：MSVC/GCC 领域检查，共享合同变化才 Integration；相关 ON/OFF 构建，插件缺失请求明确拒绝且普通场景可运行；Forward/Deferred 同输入旧/新 NPR 图、历史/resize/释放与 GUI/Job；固定 CPU/GPU P50/P95 及资源预算；支持矩阵、失败案例和阶段文档。A4-N1 当前已验收：可选 iris.npr、十个共享参数和材质 Shader 借用，两编译器各 37/37、双窗口 GPU/失败保留/保存/生命周期、NPR OFF、GUI/Job、各八张旧/新固定图、Stylized/Deferred/GPU smoke 与固定成本通过。没有改 Shader 算法或基线，没有新 ZIP；NPR 屏幕效果设置仍由后续切片迁移。

## A4-N2 屏幕效果设置适配（2026-10-14 本地验收完成）

迁移 NPR 的 Outline/Dither/Height fog/Color grading 设置适配与公开 schema。先调查 Renderer 中派生 PostProcessSettings 的 NPR 字段与现有 PostProcess 参数绑定，复用共享 RendererSettings 和 Frame/候选事务；保持后处理 Shader、FBO、纹理、历史和 Pass 调度由 PostProcess 拥有，不开发新效果，不同时迁移云水/PBR，不增加通用反射或 Render Graph。验收包括两条路径、Preset/Debug/尺寸/TAA/透明边界的同输入图，GUI/Scene/Job/失败保留，能力依赖与 ON/OFF、资源/成本预算及文档。当前已实现并完成本地验收，提交与远端检查独立核对。

## 2026-10-14 插件界面整理

插件页改为图标、名称与启用开关，参数与资源重建按插件分组移至渲染设置；关闭隐藏参数但保留值，当前管线必需项保持锁定。双编译器应用构建、MSVC Focused plugins 5/5、GCC 控件 1/1、两尺寸真实参数和资源恢复、独立 schema GPU UI 及中文/英文截图通过。无 Shader、持久化/API 或基线修改；未打包。A4-N1 的 CI 38432069907 已核对 completed/success，head c18433c；下一主线为 A4-N2。


A4-N2 将 15 个屏幕效果参数纳入 iris.npr 的 CPU schema（共 25 项），由 CPU-only applyPostProcessSettings 填写借用设置，保持 PostProcess 的 Shader/FBO/LUT/历史/Pass 所有权。旧 Scene、稀疏 N1 schema、Preset 与 RendererSettings 单一值源保留；参数页仍在 Render settings，Plugins 只做启停。

最终双编译器 Integration 各 37/37、两窗口真实屏幕参数/四种效果双路径贡献/拒绝和历史/Scene/生命周期、GUI 与两帧 Job、NPR OFF 2/2 与两路径 Job 66/无产物通过。两编译器各八张迁移图、MSVC Stylized 33 张不同 PNG 精确一致；GPU smoke、Deferred、screen-space 回归通过。120 帧预热/900 样本的四组成本满足原预算；Forward 27 Draw/167826616 bytes、Deferred 28/173817016 bytes 不变，不宣称性能加速。没有修改 Shader、阈值、基线或标准安装包；详细说明与截图保留在本地 docs/a4n2-npr-screen-settings.md。

插件 UI 的 1181b14 已推送且 CI 38544449866 completed/success，远端 SHA 一致。A4-N2 交付提交和 CI 按本次最终记录核对。下一入口是 A4 后续迁移分包调查（云水/PBR）；A4 父项和 P1/M3 保持未完成，不自动进入新研发方向。


## A4-W1：原生水面参数与着色适配（2026-10-14 本地验收完成）

调查选择水面为下一纵向切片：其共享 WaterSettings、纯 CPU Wave 函数和已绑定的 Forward/Deferred 水面 Shader 可复用 MaterialShading 合同。范围为可选静态 iris.water、19 项 schema 和 settings-only Uniform 适配，保留网格/Shader/纹理/附件/绘制/历史与云水环境的后端所有权，不迁移云层/PBR 或重写算法。

原生水面活动时依赖 Water 与 PostProcess，缺失能力在 Scene/Job/旧命令/直接 render 改变状态前拒绝；干燥/全屏场景不要求 Water。旧字段、Preset/手动编辑 Custom、共享值和两个时间输入保持。参数位于渲染设置，插件页仅做启停；Water OFF 保留 CPU schema，后端仍保留原水面资源，不宣称移除即可释放全部显存。

双编译器完整 CPU 各 37/37、两窗口真实 UI/参数贡献/拒绝/历史/保存/resize/重建/启停与两路径 GUI/Job、Water OFF 2/2/普通场景/两路径 Job 66 无产物通过。两编译器各八张迁移图及既有水面 18 张 PNG 精确一致，GPU smoke、Water mesh/medium contracts 与云水接水几何/两路径/云影贡献通过。

成本为 120 预热/900 帧、640×360、MSAA1/time0：Draw/资源不变，Forward 19/167826616 bytes、Deferred 20/173817016 bytes。首轮 MSVC Deferred GPU P95 0.479232 超出原 0.424608 ms 预算，失败保留；相同协议两次复测 0.419840/0.377856 通过。其他三组满足原 CPU/GPU 门槛；不放宽阈值、不宣称零开销或加速。详细证据在本地 docs/a4w1-water-shading-extension.md 与 .local/verification/a4w1。

前项 A4-N2 655d057 的 CI 38546907506 completed/success、head 一致。W1 提交/推送/CI 按最终交付记录核对；无新安装/ZIP。下一入口 A4-W2：先调查水面 GPU 资源与执行所有权的最小切片并固定合同。A4 父项、P1/M3 保持未完成。

## A4-W2a：水面 GPU 网格所有权（2026-10-14 已验收）

固定 High 192/Low 96 网格的 VAO/VBO/EBO 由 iris.water 拥有。后端通过显式 opengl.indexed-geometry.v1 服务短期借用 uint32 索引三角形，继续拥有 Shader、附件、绘制状态、调度和历史。原 Scene/Job/参数/网格算法保持；停用释放 1,479,184 bytes，重启事务创建，候选失败/取消保留旧资源和调用者绑定。WaterSurface 不进入 Water OFF 应用。

功能检查通过：双编译器 CPU 各 37/37；两编译器真实 GL 布局/内存/取消/替换/销毁/重启/绑定；双窗口 UI 与两路径 GUI/Job；最终 OFF/PostProcess ON plugins CPU 5/5、普通场景和两路径 66/无输出；GPU smoke/Water contracts/云水集成；34 张迁移 PNG 精确一致。

历史门槛诊断：四组 CPU/Draw/活跃资源 bytes 通过，GPU P95 曾均超出原预算。MSVC Forward 首轮 0.340992 ms，两次复测 0.495616/0.525312 ms，预算 0.321184 ms。同机旧 W1 的 P95 也升至 0.476160/0.351232 ms，新程序交叉为 0.360448/0.360448 ms；遥测出现 930～2490 MHz 频率变化，不能因此直接宣布固定预算通过。原 before/阈值未改，失败和交叉诊断全部保留在 .local/verification/a4w2a，本地文档 docs/a4w2a-water-grid-ownership.md。

按 2026-10-14 用户明确授权“有问题或者没有价值的验收测试直接摒弃”，撤弃 WaterMigrationAcceptance 的跨运行历史 GPU P95 门槛：未变的 W1 负对照也反复被其判失败，无法可靠判断这次所有权迁移是否引入回归。GPU 时间继续诊断记录，旧失败和 before 保留；CPU P95/Draw/bytes/精确图像/真实 GL 合同不变。仅重跑修改脚本，两编译器各八图和四组 CPU/资源预算通过，W2a 完成；不宣称 GPU 性能已通过认证。17e2f38 CI 38566799577 已核对 completed/success。下一入口 W2b 两套水面 Shader 所有权迁移，绘制/附件/状态/历史继续由后端管理。A4 父项、P1/M3 未完成，无新安装/ZIP。


## A4-W2b：水面 Shader 所有权（2026-10-14 已验收）

Water 插件拥有 Forward/Deferred 两套 Shader 与 High/Low 网格，新增 opengl.shader-borrow.v1 明确后端短期借用。候选先完整编译/准备再发布；失败/取消保留旧实例和外部绑定。后端按实际路径借用并继续管理绘制、附件、状态、Camera/环境/时间与历史；Shader 文本与 19 项共享参数/Scene/Job 不变。程序继续使用既有全局原子热重载，失败保留两套旧程序、修复后一起替换；干燥/OFF 不自动创建水面程序。

验收：双编译器 CPU 各 37/37；真实 GL 程序/网格生命周期、第二套编译失败、原子热重载失败/恢复、取消/替换、外来 Shader/缺失服务/源文件与绑定保留；双窗口 UI、两路径 GUI/Job、OFF CPU 5/5/普通场景/66 无产物；GPU smoke/云水集成；34 张受影响 PNG 精确一致，CPU 原预算及 Draw/活跃 bytes 保持。GPU 时间仅诊断。按用户授权摒弃不成立的驱动链接失败夹具，修正私有夹具 mtime，相关失败记录保留；没有弱化真实资源/编译失败合同。

首次启用时有一次上下文线程编译；停用释放两套程序和网格，程序驱动内存不计入 1,479,184 bytes 网格估算。来源与命令在本地 docs/a4w2b-water-shader-ownership.md 与 .local/verification/a4w2b。前项 1fc3e12 CI 38577302174 已核对 completed/success。W2b 提交/推送/CI 分开核对；无新安装/ZIP，A4 父项/P1/M3 未完成。

下一入口 A4-W2c：调查水面绘制执行与环境/灯光/阴影/云纹理借用最小切片，固定实际 Forward/Deferred 目标、状态和历史合同；不自动扩大为云层/PBR 或渲染图重构。

## A4-W2c：水面绘制边界调查（2026-10-15 已完成）

源码审计基于 f19ed96：Deferred 管线的水面颜色仍走 Forward Shader；仅在 Deferred + TAA 时先用水面 G-buffer Shader 补写运动附件 3，另三路 GL_NONE、深度不写，再回 scene FBO 绘制颜色。bindRefractiveScene 先把 opaque 颜色/深度/模板复制到独立 scene 目标；opaqueColor 与 sceneDepth 只供采样。surfaceOptics 的云 march 在折射前执行，运动历史仍由后端帧末统一更新。纹理输入为 unit 0/1 CUBE_MAP 环境、2 级联 2D_ARRAY、3/4 opaque 颜色/深度、5 云影、6 BRDF LUT、7 云反射；云影还依赖投影 Uniform，不能仅借一个纹理 ID。

选定下一工作包 **A4-W2c-1：水面 indexed draw 所有权**。只把网格选择、几何校验、VAO 绑定/一次 glDrawElements/解除到 VAO 0 移入 Water 插件；后端保留 Shader/Uniform/纹理准备、目标/draw buffers/viewport、GL 状态、调度和历史。新增独立 Surface stage 与 opengl.indexed-draw.v1 服务、drawIndexedGeometry(RendererSettings) 返回本次 draw 数；基础实现拒绝不支持的调用。renderFrame 继续只写 Uniform，MaterialShading 保持原合同；Surface 暂不接受逻辑纹理资源或自有 pass。保留已交付的几何/Shader 借用 API，不新增通用状态快照或环境输入框架。

下一项门槛：双编译器领域/共享接口 Integration；真实 GPU High/Low 绘制、运动附件隔离、调用前后外部 program/FBO/纹理/状态保持与 VAO 0、未准备/缺失服务拒绝及候选事务；两路径 GUI/Job/TAA/Resize/启停/OFF；两编译器各八张迁移图及受影响水面 18 图一致、GPU smoke/云水集成、CPU/Draw/bytes 原预算。GPU 时间仅诊断，不恢复已撤弃的历史 P95 门槛；不添加只复述调用计数的无效测试。

本次仅交付源码审计和合同冻结，没有运行时代码变更，未重复构建/GPU/性能/打包。详细证据为本地 docs/a4w2c-water-draw-boundary.md 的所有权矩阵与调查命令；W2c-1 尚未实施，A4/W2 父项及 P1/M3 保持未完成。前项 f19ed96 的 CI 38584152494 已核对 completed/success。

## A4-W2c-1：水面 indexed draw 所有权（2026-10-15 已验收）

Water 改用独立 Surface stage 和 opengl.indexed-draw.v1，插件选择 High/Low 网格、验证 uint32 三角形索引并执行 draw，返回次数供后端统计。后端不再直接使用水面网格，继续准备 Shader/Uniform、环境纹理、目标/draw buffers/viewport 和 GL 状态，拥有调度与历史；Deferred + TAA 仍先仅补运动附件，再用 Forward Shader 绘制颜色。旧几何/Shader 借用接口保持，MaterialShading 仍只写 Uniform；Surface 拒绝自有逻辑纹理/pass，未准备或不支持的 draw 明确拒绝，缺失服务在资源创建前拒绝。

双编译器 Integration 各 37/37；真实 GPU 两种网格的实际图元数/运动像素、材质与深度隔离、外部 program/FBO/纹理/状态保持与 VAO 0、缺失服务/失败/热重载/取消/替换/释放通过；两窗口 UI、Forward/Deferred GUI/Job、OFF CPU 5/5/普通场景/66 无帧输出通过。34 张受影响 PNG 精确一致，GPU smoke 和云水几何/两路径/云影贡献通过。

四组 CPU/Draw/活跃资源 bytes 原预算通过。MSVC Forward 初次 CPU P95 3.8884 ms 和首次复测 3.3290 ms 超过原 3.3245 ms 上限，全部保留；随后固定两次同协议结果 2.0105/1.7402 ms 均通过，未改阈值或基线，不宣称性能提升。GPU P50/P95 仍仅诊断。PolygonMode 夹具不再假定查询必定写回两个值，改为比较调用前后的实际状态；纯 CPU 注册表测试的无关 GL include 已撤回，拒绝断言放到现有 GPU 夹具。失败日志保留，本地阶段说明为 docs/a4w2c1-water-indexed-draw.md，证据为 .local/verification/a4w2c1。

前项调查提交 5ef02fd 的 CI 38664009109 completed/success，head 一致。本项源码和本地验收完成，提交/推送/CI 独立核对；未安装/打包，A4/W2 父项和 P1/M3 保持未完成。下一入口 A4-W2c-2：调查水面逐帧 Uniform 适配的最小切片，固定相机/时间/历史与环境依赖后再实施。

## A4-W2c-2：逐帧 Uniform 边界调查（2026-10-15 已完成）

基于 5c9ffba 追踪水面七项公共输入：当前/前帧 VP、CameraPosition、当前/前帧水面时间、CameraSurfaceHeight 与 MotionHistoryValid。当前 VP 已带后端 TAA jitter，不能从 Camera 重新计算；前帧 VP 与前帧时间各按不同 valid 标志回退，运动有效性保留 resetTemporalHistory 和 abs(time-prev)<0.2 s。帧末历史推进仍由后端统一管理，两次子调用共享同一帧解析结果。

下一实施包 **W2c-2a：七项公共逐帧 Uniform 适配**。以只读 WaterFrameInputs 短期借用传入五字段：两个 VP 矩阵、已解析前帧时间、相机水面高度、运动有效标志；Camera 与当前 WaterSettings 时间复用 RenderPluginFrame 现有来源。现有 Frame 构造函数末尾追加默认 nullptr 参数，保持旧 Settings-only 调用；生产水面调用显式传入，WaterRenderer::renderFrame 执行七项写入。无需新增 virtual、服务或第二份历史。Forward 专用相机/逆投影参数、环境/灯光/级联/云影/纹理仍由后端绑定。

下一项门槛：双编译器共享 Frame Integration、旧调用兼容；真实 Uniform 与带 jitter 矩阵/时间一致、运动有效/重置像素、后端回退和阈值的真实路径验证、既有状态/附件隔离/资源事务；相关 GUI/Job/TAA/Resize/水面介质与 OFF、34 张受影响固定图、gpu-smoke 和原 CPU/Draw/bytes 门槛。云水集成仅在环境绑定变化或疑点时补跑，不机械扩大到所有历史专项；GPU 时间仍诊断，无新基线或 ZIP。

本次只完成源码审计和合同冻结，无运行时代码/测试变更，不重复构建/GPU。详细本地记录：docs/a4w2c2-water-frame-uniform-boundary.md。W2c-2a 尚未实施，A4/W2 父项、P1/M3 未完成；上一项 5c9ffba 的 CI 在本次开始核对时仍 in_progress，结论单独等待。

## A4-W2c-2a：七项公共逐帧 Uniform 适配（2026-10-15 已验收）

WaterFrameInputs 五字段只读借用保留后端已解析的当前/前帧 VP、前帧时间、相机水面高度和运动有效性；WaterRenderer 复用 Frame 的 Camera 与原 WaterSettings 当前时间，执行七项 Uniform 写入。Frame 构造函数末尾默认 nullptr 保留旧 Settings-only 调用；插件不缓存借用。后端保留 TAA jitter、两个独立回退、0.2 s 判断及帧末历史，现有只读诊断新增实际当帧输入包，不将其作为下一帧历史来源。Forward 专用相机/环境/灯光/纹理、附件/GL 状态/调度仍在后端，Shader、19 项 schema 与 Scene/Job 不变。

最终 MSVC/GCC Integration 各 37/37；真实 GPU 活动 Uniform 与 jitter 矩阵/独立时间源/旧空借用一致，运动有效/无效/相同帧的实际像素、状态/附件及资源事务通过。两编译器两窗口真实后端正负 0.2 s 边界、独立 VP/时间回退、Resize 与介质切换、GUI/Job，OFF CPU 5/5/普通场景/66 无帧输出通过。34 张 PNG 精确一致，GPU smoke 通过；四组 CPU/Draw/bytes 原预算首轮通过，GPU 仅诊断。无新失败、未放宽阈值或撤弃测试；绑定未变且无疑点，不机械重复云水/完整图像/纯 Wave GPU 套件。

本地证据为 docs/a4w2c2a-water-frame-uniforms.md 与 .local/verification/a4w2c2a。前项 228c8ca 的 CI 38665906290 completed/success；本项源码和本地验收完成，提交/推送/CI 单独核对。无新安装/ZIP，A4/W2 父项、P1/M3 保持未完成。下一入口 W2c-2b：Forward 专用相机和逆投影 Uniform 适配，先固定输入合同再实施，环境/灯光/纹理继续单独推进。
