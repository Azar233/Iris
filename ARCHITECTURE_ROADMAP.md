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

PBR/NPR 是着色或管线能力，光追是执行技术，可以组合；后端、管线和效果不混成互斥枚举。Material/Light/Camera 数据合同保持共享，CPU/GPU 支持差异明确诊断。现有 SceneModule 只控制运行场景/模拟，不直接扩充为持有 GL 对象的渲染插件。

## A2 分包与当前范围（2026-10-08）

A1 的 GitHub CI 已成功，当前实现基准为 `38b1995`。A2 分成两个独立验收工作包，父项在全部完成前保持未完成：

- **A2-A（2026-10-08 本地验收完成）**：插件 API v2、Input/Output/Transient/History 的有序逻辑 Pass 合同、借用纹理类型/尺寸/别名校验；共享后处理管线已迁到第二个可选 Target。双编译器 ON/OFF、最终 28/28、真实 GPU 资源消费/拒绝/历史/resize/重建、固定图 SHA 相同、十套渲染/Stylized/CPU PT 回归通过，默认构建恢复 ON。无 Shader 算法或基线修改。
- **A2-B1（2026-10-08，GUI 启停切片）**：Inspector 的 Plugins 页统一显示静态插件目录、编译可用性和当前管线用途；启停通过共享 EditorCommand 校验，随 Scene 保存。当前管线必需项锁定，切换到停用能力或加载无效配置时保留当前状态；停用未使用能力在下次 Raster 帧的上下文线程释放实例，重新启用按需创建。CTest 新增真实 ImGui 输入注入与配置事务测试；GPU 命令/保存重开/释放重建由 tools/PluginGuiAcceptance.ps1 验证。
- **A2-B2（随后，未完成）**：通用参数持久化与动态参数面板、Shader 编译失败后的事务式资源替换、完整输入资源声明。启停配置不等于通用参数平台；父项 A2-B/A2 继续未完成。复用已有 ParameterRegistry/EditorCommand，避免另建 Scene 或参数真相来源。
  - **A2-B2a（2026-10-10，验收完成）**：ParameterRegistry 成为模块与渲染插件共享 target；descriptor 声明参数元信息与绑定，Inspector → Plugins 自动生成面板。两个现有插件共 18 个参数，新 Scene 参数块 version=1，旧字段兼容读取；绑定修改同一 RendererSettings，未知/重复/类型/范围错误事务拒绝。双编译器 ON 全构建各 32/32、两插件 OFF 参数/控件测试、真实 GPU 贡献/旧字段等价/保存重开、双尺寸与 260 px 长标签、真实 ImGui Bool/Float 输入、Raster/CPU Job 无产物拒绝、十套图像回归与 CPU PT 回归通过，既有固定图 SHA 不变。源码与共享进度在当前实验分支一并交付，新提交 CI 状态单独核对。
  - **A2-B2b1（2026-10-10，验收完成）**：同一上下文线程中变更/待处理 Shader Program 整批准备与发布，编译或链接失败保留所有旧程序与历史；修复 include、创建缺失依赖或释放阻塞 owner 后可恢复。构造/候选失败采用 RAII 清理；Renderer 面板显示日志并支持真实命令重试。双编译器全构建、CTest 各 32/32、真实 GL 编译/链接失败与恢复、跨 Enscape/Postprocess 旧图保留与提交贡献、双尺寸重试 UI、GPU smoke、十套既有图像回归及两插件 GPU 生命周期专项通过。未改 Shader 算法、阈值或基线。全局批次可能被闲置插件错误阻塞；这不是整个插件实例/纹理的事务替换，未新增性能测量或发布包。
  - **后续切片**：独立插件参数值与可扩展 schema 目录、资源参数/版本迁移、剩余资源声明及插件整体替换合同。当前参数绑定仍使用 RendererSettings 兼容桥，通用参数/资源平台未全部收口；父项 A2-B2/A2 保持未完成，不因此开启 A3/A4。

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
- A2-A：后处理第二插件与资源/Pass 合同本地验收及 GitHub CI 通过，提交 71c866a。A2-B1：GUI 启停与 Scene 配置切片本地验收及 GitHub CI 通过，提交 4ba850c；最终双编译器 30/30、真实 ImGui 输入、应用命令/GPU 释放重建/保存重开、Job 无产物拒绝、GPU smoke 与十套既有图像回归通过。MSVC 默认 NPR 迁移前后图 SHA 相同，跨编译器横向对比不宣称逐位一致；没有改 Shader 或基线。下一项 A2-B2，父项 A2 未完成。A3/A4/P1/M3 尚未开始。
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
