# Iris 项目目录与验证入口

> **M3-D4 交互与交付（2026-10-29）**：移动重启排空已提交样本、保留有效预览，完成数与新相机SPP隔离，Frame/取消历史合同保持。两编译器Integration40/38、严格Temporal/Spatial/Guides和gpu-smoke通过；46组720p两轮整帧/显存、八组同终点质量、实际UI/Queue、带降噪Capture和独立M3D4 ZIP交付。时间＋空间移动整帧约0.33～0.84秒P50，未达30FPS；Depth3/4截断、透射Raw和纹理软化保留。下一唯一入口P5统一优化，P5/M3-P/M3仍未完成；未提交推送。见 [D4](docs/m3d4-interactive-delivery.md)。

> **M3-D3 本地收口（2026-10-29）**：可选GPU时间重投影、双线性逐点身份/深度/法线拒绝、矩与邻域历史约束接通；同一Raw不重复计数，Scene/Viewport/Job/Queue可对照Raw/Accumulated/Denoised。共享CPU MSVC40项覆盖/GCC38、最终Focused各9、双编译器严格GPU/78对运动EXR/192原AOV、三场景720p/实际平移与开关/Frame跳切/Queue14文件通过。时间累积RMSE下降约34%/27%/57%；Sponza叠加空间反而变差，透射Raw与约66ms时间pass成本明确保留。当前唯一入口D4交互与交付，P5后续统一优化；未打包/提交/推送。见 [D3](docs/m3d3-temporal-denoise.md)。

> **M3-D2 本地收口（2026-10-29）**：可选GPU两轮A-Trous Beauty，直接复用驻留Guide/Raw，Scene显示开关与Job/Queue显式denoised-beauty输出接通；旧Raw/AOV不变。双编译器Integration40/38、最终Focused各9/9、严格GPU解析/恢复、96原AOV同图、三场景720p/真实UI切换/Queue两帧通过。4SPP对64SPP参考RMSE降低约17%/29%/62%，高频纹理软化与透射/1SPP旁路明确保留；成本单列，不是30FPS。按用户要求P5后续统一优化，该包完成时入口D3，当前入口见顶部D3记录；未打包/提交/推送。见 [D2](docs/m3d2-spatial-denoise.md)。

> **M3-D1 本地收口（2026-10-29）**：可选GPU Motion/前后depth-normal/实体与mesh-material身份，复用AS/几何与共享Timeline；Core Integration40/38、严格GPU、192原AOV保持与6对Guide一致；最终UI补Focused各5、GL显示1/255/三窗口、双编译器实际Frame平移跳切和Queue两帧通过。720p Guide GPU P50约24～31ms、同步CPU约282～294ms，后续D2直接复用GPU缓冲，未达成30FPS。P5两项失败保留，该包完成时入口D2，当前见顶部D2记录，未打包/提交/推送。见 [D1](docs/m3d1-motion-history.md)。

> **M3-D0 本地收口（2026-10-28，按用户要求先推进下一阶段）**：Job/Queue、GPU后端和Viewport共享1280上限，原生1280×720/竖屏/旧1024方图/1×1及容量/非法尺寸保留旧图通过。修复固定720p在Viewport裁切，Overlay显示实际尺寸，鼠标/标记随显示矩形换算。双编译器Integration40/38、最终Focused各5/smoke；原144 AOV不变、新720p96文件/48对同图、实际Queue两尺寸与三场景64SPP大图/三窗口通过。720p Depth8有效SPP/s约2.82/8.69（Sponza/Glass），不宣称30FPS；P5两项性能失败、M3-P/M3未完成保留。该包完成时下一入口D1，当前见上方D1记录；未打包/提交/推送。见 [D0](docs/m3d0-native-720p.md)。

> **M3-P5 整体性能复核（2026-10-28）**：原P0协议下Sponza完整64SPP均值155.934→18.830s、有效约4.215SPP/s，追踪分配约1.90→0.96GB；新一轮仍有经典Sponza短协议GPU5.433856ms>5.3647104ms、Vulkan RQ切换318.4855ms>307.3422ms两项失败。保留原门与全部失败，长协议不替代短协议。P5/M3-P/M3未完成，D阶段进度见上方记录；Capture与独立包按本阶段单独核查，不把打包当性能通过。见 [P5复核](docs/m3p5-overall-delivery.md)。

> **M3-C 本地收口完成（2026-10-27，新场景范围）**：GPU PT 已接通 PBR 纹理、显式灯/HDRI、发光面 NEE/MIS、Transmission 与单活动吸收介质、后台 Queue。Sponza / Stanford Glass / Opaque 的固定协议收敛及 Strict Core/Sync 通过；MSVC/GCC CPU 40/40、38/38，最终 GCC 八类 PNG 与 MSVC 一致，OFF 返回 70 无输出。三场景已按用户要求用铺满 Viewport 的 1920×1080 工作台、内部 1024×578、64 SPP 检查；Queue 实际按钮与 1440/1100 两尺寸通过。最终带 BLAS/TLAS Build 与 TraceRays 的 Capture、独立 ZIP 包内输入/48 AOV/大画幅保存重开同 PNG/缺失 SPV 拒绝均通过。旧 cube 一次 1.43×10⁻⁶ 的照明微差保留诊断及原断言，不改阈值或旧基线，不再追加旧 fixtures 图像套件。M3-C 四项完成，M3/M3-D 保持未完成；旧 ZIP 保留，未提交/推送。见 [M3-C](docs/m3c-gpu-path-tracer.md)。

> **图像验收画幅（2026-10-27，用户要求）**：人工画质和工作台验收使用铺满可用 Viewport 的画面，取消 `MYRENDERER_RENDER_WIDTH/HEIGHT=160/90` 一类小尺寸覆盖。优先记录 1920×1080 工作台，并保留 1440×900 / 1100×680 布局检查；注明实际内部渲染尺寸与采样数，GPU PT 的最长边 1024 限制仍如实记录。96×54 等小图只用于固定预算数值收敛，不作为人工画质验收或说明截图。

> **M3-C1 基础 GPU PT（2026-10-26）**：真实 Ray Tracing Pipeline 已接入不透明无纹理共享网格、Workspace 与 GPU Job 21。固定 Seed 渐进采样、Raw Beauty/八类 AOV、变化重启、取消及 PNG/HDR/EXR 导出通过。MSVC/GCC 全量 CPU 40/38、双生命周期 Core/Sync 零告警、双尺寸实际控件、GUI/Job 与独立 Runtime 保存重开同图通过；1/8/64 SPP 线性 RMSE 为 0.0568/0.0431/0.0172。OFF 明确拒绝 GPU 输出。M3-C 的三 Reference 场景、纹理/Transmission/介质、Queue 和阶段 Capture/发布仍未完成，M3-D 未开始。旧 ZIP 哈希保持不变；本轮未生成新 ZIP、提交或推送。见 [M3-C1](docs/m3c1-gpu-path-tracer.md)。

> **M3-B Ray Query（2026-10-26）**：方向光硬阴影、可见性调试、共享局部 Mesh BLAS 与实例 TLAS 已接入 Workspace/Scene/Job 19/20，默认关闭。双编译器 Core/Sync、9856 像素 CPU oracle、双尺寸实际控件、GUI/Job、两轮 121 帧/P1 CPU 姿态哈希、Reset/回退/活动取消通过；刚体仅 UPDATE TLAS。既有完整回归、带 AS Build/Pass/Resource Label 的 Vulkan Capture 和独立安装保存重开同图已通过。CSM / Vulkan OFF / Ray Query 实测成本与最终 ZIP 见 [M3-B 验收](docs/m3b-ray-query-shadows.md)；本机 CSM 更快，保留同步回读/逐帧资源准备限制。M3-C/D 未开始，旧 P1/M3-A ZIP 保留；未提交/推送。

> **M3-A Vulkan Raster（2026-10-26）**：共享静态/刚体 ModelData、Camera、PBR/Light/HDRI 已接入 Workspace 与 schema 4 Raster Job。MSVC/GCC 严格 Core/Sync、resize/失败恢复/销毁、双尺寸实际菜单往返与同图通过；P1 两轮 121 帧、CPU 姿态哈希、Reset/回退与取消通过。既有 63 次冻结图像比较、Stylized/Module/CPU PT、最终 gpu-smoke、性能/内存与带标签 Capture 已记录。独立安装验证包内资源、GUI 保存重开同图和 CPU Job，无 SDK/源码资产回退。最终 ZIP 和收口状态见 [M3-A 验收](docs/m3a-vulkan-raster.md)。M3-B/C/D 保持未完成，保留 P1 原归档；未提交/推送。

> P1 当前状态（2026-10-26）：最小 CPU 刚体已完成本地验收与 ZIP 交付，MSVC Release / PhysX ON，GCC Release / PhysX OFF。验证入口为 physics-acceptance、physics-trajectory 及 tools/PhysicsRenderingAcceptance.ps1 / PhysicsPackageAcceptance.ps1；证据与 ZIP 在 .local/verification/p1-close/。下方 P1-A/B 文字保留切片历史，不再表示产品未接入或发布未检查。见 [P1 收口](docs/p1-close-acceptance.md)。

> M3-A1（2026-10-26）：两套主缓存显式开启 `IRIS_ENABLE_VULKAN=ON`，默认选项仍为 OFF，`build-stored-off` 明确关闭。`vulkan-smoke` 是独立开发 GPU 入口，串行执行；证据位于 `.local/verification/m3a1/`。仅覆盖设备/交换链/Dynamic Rendering 清屏、双帧同步、resize/回读/销毁。Validation Layer 缺失，严格入口 `IrisVulkanSmoke --require-validation` 会拒绝；见 [M3-A1](docs/m3a1-vulkan-bootstrap.md)。P1 ZIP 保持原归档。

> M3-A2（2026-10-26）：补齐项目本地 Khronos Validation Layer 1.4.366，修复 Acquire/布局转换依赖。使用 PowerShell 7 执行 `tools/Verify-Vulkan.ps1 -Compiler msvc -LayerDirectory .local/tools/vulkan-validation-20261025`，或指定 `-Compiler gcc`，串行构建并严格检查核心/同步验证。脚本只向子进程传入 `VK_LAYER_PATH`，不改注册表或全局环境；双编译器每轮均 0 Warning / 0 Error。证据与缺失能力拒绝检查见 [M3-A2](docs/m3a2-vulkan-validation.md)，覆盖上方 M3-A1 的验证层缺失状态。

P1-A 新增可选 `IRIS_ENABLE_PHYSX`（默认 OFF，当前 ON 仅支持 Windows/MSVC x64 Release）与 `Verify-Iris -Area physics`，对应 `physics-sdk-smoke` 开发检查。开启使用 `cmake --preset msvc -DIRIS_ENABLE_PHYSX=ON`，仍复用现有暖缓存；GCC 保持 OFF。该 SDK 切片尚未接入产品 Scene/Job，不代表物理阶段验收完成。

P1-B/C 已进一步接通共享刚体组件、`iris.physics.rigid-body` Module 和 Scene/Job，`physics` 领域新增 `physics-module`，OFF 构建也保留 schema/事务/拒绝诊断检查。示例 `12_cpu_rigid_bodies.renderjob` 的缓存与输出写入 `.local/physics/rigid-bodies/`；源码产品已接入，不表示现有安装包已更新或物理阶段验收完成。

源码和测试输入保留在 Git 中；机器相关缓存、编译输出、偏好设置和生成证据分别管理。目录名 `MyRenderer`、既有 CMake target、`MYRENDERER_*` 验收入口及旧 Scene/Job 格式是兼容接口，程序和新产物使用 Iris 名称。

| 位置 | 用途与保留策略 |
| --- | --- |
| `src/`、`plugins/`、`shaders/`、`assets/` | 当前核心实现、插件、Shader、示例与许可；不按文件名猜测删除资产 |
| `tests/` | 单元测试、契约与版本化回归基线 |
| `cmake/`、`tools/`、`.github/` | 配置、测试领域索引、资源同步和 CI |
| `build-ci-msvc/`、`build-mingw/` | 当前 MSVC/GCC Release 缓存；绝对路径绑定，保留可避免冷启动编译 |
| `build-stored-off/` | 可选插件 OFF 专项缓存，相关变更才启用；实际开关以 CMakeCache 为准 |
| `.cache/` | 可重建的本地依赖/工具缓存 |
| `.local/verification/` | 每个编译器/领域/级别的最新计划与日志，同一入口覆盖旧临时日志 |
| `.local/evidence/` | 已归档截图、报告和可编辑素材；旧路径映射在 `.local/maintenance/cleanup-manifest.json` |
| `.local/captures/`、`.local/benchmarks/` | 新的默认截图和手动 benchmark 输出 |
| `.local/install/Iris/` | 当前安装目录，按发布或资源路径任务刷新 |
| `.local/tools/` | 本地 GPU Capture 等工具 |
| `docs/`、`.agents/`、`AGENTS.md`、`todolist.md` | 本地说明、协作规范和路线图，保持 Git 忽略；历史证据不等于当前验收 |

Windows 偏好设置位于 `%LOCALAPPDATA%/Iris/`：`editor.ini`、`language`、`recent-scene`、`render-queue.json`。旧工作目录的 `MyRenderer.editor.v2.ini`、language/recent-scene，以及旧全局队列会在新文件不存在时复制迁移，现有 Iris 文件优先。可用 `IRIS_STATE_DIRECTORY` 隔离实验/测试；队列优先使用 `IRIS_RENDER_QUEUE_STATE`，旧 `MYRENDERER_RENDER_QUEUE_STATE` 继续兼容。自动 smoke/benchmark 不改最近场景，回归窗口不保存交互布局。

## 选择最小验证范围

先指定变更领域，再选择级别。默认 `Focused` 只构建 CMake 声明的测试拥有目标并执行对应 CTest 标签。`Integration` 执行一次全量 CPU；`Release` 加产品构建和打包。GPU 目标显式传入，逐个串行执行；不会自动插入十套图像、性能 sweep 或打包。首次配置使用锁定依赖，重复配置的 Build ID 根据源码/编译器/选项/配置保持稳定，真实源变化会失效。

```powershell
cmake --preset msvc
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area shader-scene -PlanOnly
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area shader-scene
powershell -NoProfile -File tools/Verify-Iris.ps1 -Compiler gcc -Area scene
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area plugins -Compiler plugins-off
powershell -NoProfile -File tools/Verify-Iris.ps1 -Level Integration
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area shader-scene -GpuTargets shader-scene-gpu-acceptance
powershell -NoProfile -File tools/Verify-Iris.ps1 -Level Release
cmake --install build-ci-msvc --config Release --component Runtime --prefix .local/install/Iris
```

领域：`core`、`state`、`scene`、`assets`、`jobs`、`plugins`、`shader-scene`、`path-tracing`、`optics`。共享 Scene/Job/参数/API 变化在提交前跑一次全量 CPU，并检查另一编译器；渲染行为变化仍需要真实 GPU 与受影响图像，广泛集成/阶段收口才扩展完整图像套件。GPU 时间和性能对照单独执行，避免测试/构建抢占；发布必须检查独立目录启动、Job 与许可。

同一最终源码已经通过的检查只在新增相关变更、失败或未解决疑点时重跑。纯文档不启动编译和 GPU；UI 偏好路径或外观变化无需重复完整云/玻璃/PT 图像套件。跨构建目录的 CPU CTest 串行，避免共享临时夹具冲突。

发布 ZIP 使用 `cpack -C Release --config build-ci-msvc/CPackConfig.cmake -B .local/packages`，避免在根目录留下 ZIP 和 `_CPack_Packages`。安装时指定 `Runtime`，避免引入依赖库的开发头文件和静态库。

普通开发分支 CI 保留构建和完整 CPU 检查，不再每次生成发布 ZIP。`v*` 标签或手动工作流的 `package=true` 执行 CPack/上传；手动入口依赖工作流进入默认分支。并发新提交取消同一分支的旧 CI。GPU 检查仍在本地完成，CI CPU 通过不代表 GPU 通过。

A4-N1 的 NPR 移除专项使用 cmake --preset plugins-off -DIRIS_ENABLE_POSTPROCESS_PLUGIN=ON，保留普通原生 Raster 依赖的 PostProcess，关闭 Enscape/NPR；默认 preset 仍关闭全部可选插件。复用当前 build-stored-off 前核对这些实际开关，不能把该专项称作全插件 OFF。标准安装/ZIP 仍是 A3-M2，A4-N1 开发程序位于 build-ci-msvc/Release/Iris.exe；本切片未打包。

A4-W1 新增 IRIS_ENABLE_WATER_PLUGIN，msvc/gcc presets 为 ON、plugins-off 为 OFF。水面移除专项仍使用 cmake --preset plugins-off -DIRIS_ENABLE_POSTPROCESS_PLUGIN=ON，关闭 Enscape/NPR/Water，保留普通原生管线必需的 PostProcess；CPU schema 始终编译。Build ID 纳入 Water 开关，三套暖缓存继续复用。
