<p align="center">
  <img src="assets/icons/iris-source.png" width="96" alt="Iris 鸢尾花图标" />
</p>

<h1 align="center">Iris</h1>

当前研发转向可扩展引擎架构，暂停场景画质扩展；计划和验证状态见 [插件架构路线](ARCHITECTURE_ROADMAP.md)。实验分支不自动合并 `main`。现有图像与场景保留作展示和迁移回归输入。

2026-10-10 进度：A1、A2-A 与 A2-B1 已验收；新增 Point/Spot 灯光实体（创建、编辑、启停、保存与实时/CPU 渲染）和 G-buffer/SSAO 通道预览及 PNG 导出。A2 静态 OpenGL 核心扩展合同已本地验收，下一主线为 A3 用户 Shader；局部灯光阴影、体积光和 Vulkan 尚未实现。

A2-B2a 已完成现有两个插件的 18 个声明式参数：在 Inspector → Plugins → Parameters 中编辑，随 Scene 保存，错误输入保留原状态；仍复用共享 RendererSettings。新增 A2-B2b1 多程序 Shader 事务重载：整批成功才发布，失败保留旧画面与历史，可在 Renderer 面板查看日志并重试。独立值存储及剩余资源合同已在后续 A2 切片收口。

A2-B2b2 提供未绑定参数的独立值存储与可扩展 CPU schema 目录：新 C++ 插件可声明参数、通过共享命令编辑、随 Scene 保存并从 frame 读取，无需逐参数扩充宿主字段。现有 18 个参数保留兼容绑定；Asset 资源参数与显式 schema 迁移已接入，用户插件发现留待 A3。

A2 收口提供完整候选插件的 Shader/FBO/纹理准备与失败保留、取消/过期检查、全/半分辨率输入校验及 Plugins 页的资源重建命令。三种构建 CTest 各 32/32，真实 GPU、旧输入/Job、图像与 CPU PT/Module/云回归、带标签帧捕获和既有 GPU 性能门槛通过；源代码与 CI 交付状态见共享路线。

A3 首个单 Image 切片提供版本化 `.shaderscene` 包、严格路径/float 参数校验、项目 Camera/时间/分辨率输入，以及打开、拖入、`.myscene` 相对引用保存与失败保留。运行 `build-ci-msvc/Release/Iris.exe assets/shader-scenes/orbit/orbit.shaderscene` 可预览自制 Orbit 包；新包只需 JSON 与 GLSL。该切片支持 GLSL `main()` 与 display RGBA8 预览/PNG，多 Pass/反馈见下方 S1-A2a；参数覆盖 UI、Job 和模型材质 Shader 尚未实现。模板与输入约定见 [Orbit 包说明](assets/shader-scenes/orbit/README.md)。

A3 / S1-A2a 增加 `.shaderscene` v2：最多 4 Pass、每 Pass 4 个二维 Pass 通道、全/半尺寸线性缓冲与显式上一帧反馈。重复时间不会累计，Camera/尺寸/时间跳转/成功重载和显式失效清历史；失败保留旧完整运行状态。运行 `build-ci-msvc/Release/Iris.exe assets/shader-scenes/feedback/feedback.shaderscene` 查看三 Pass 自制示例，约定见 [Feedback 包说明](assets/shader-scenes/feedback/README.md)。S1-A2b1 为 v2 增加 `entry: "mainImage"`，宿主包装每个 Pass 的 `mainImage(out vec4, in vec2)`，使用相同 iris Uniform、编译检查和原子重载，示例见 [mainImage 包说明](assets/shader-scenes/main-image/README.md)。通用参数 UI、Job 与模型材质 Shader 仍属后续工作，A3 尚未收口。

S1-A2b2 的 `.shaderscene` v3 新增最多 8 个有界纹理资源：二维 PNG、二维/三维 raw、固定 uint32 Seed 的 hash-v1 噪声，支持 linear R8/RGBA8、二维 sRGB、Mip 与每通道的过滤/Wrap。总纹理预算含 Mip 为 64 MiB；资源变化核对内容身份，失败保留旧完整运行状态，重新打开成功后清历史。运行 `build-ci-msvc/Release/Iris.exe assets/shader-scenes/textures/textures.shaderscene` 查看自制颜色/体积切片/噪声示例，完整格式与边界见 [纹理包说明](assets/shader-scenes/textures/README.md)。v1/v2 保持兼容，纹理文件支持完整包热重载或重新打开更新，通用 ShaderScene Job 仍明确拒绝。

S1-A2b3 将 01/03 的 Enscape 四 Pass 迁入 [Enscape ShaderScene 包](assets/shader-scenes/enscape/README.md)，继续使用共享 Camera 和已有 13 个参数（含 `noiseReduction`）、保存重开、模式切换及 07 Raster Job。v3 的可选 CPU `parameterSource` 和 bool 参数连接兼容值；无新引用的旧 Scene 仍使用原插件。新旧同输入图像/24 帧和成本通过固定门槛；只对 previous 引用分配双缓冲，私有纹理预算不增加。独立 ShaderScene Job 仍拒绝；S1-B 已接通分类、声明依赖、源码操作、原始诊断、公开参数和完整包自动事务重载；可保存的历史策略已接入，任务闭环继续推进。

<p align="center">A C++17 real-time and offline rendering playground.</p>

<p align="center">
  <a href="#快速开始">快速开始</a> ·
  <a href="#主要能力">主要能力</a> ·
  <a href="#构建与测试">构建与测试</a>
</p>

Iris 是一个独立的图形学与渲染实验平台，将实时光栅化、CPU 路径追踪、场景编辑和批量渲染放在同一套工作流中。

项目源于我在西安交通大学图形学课程框架 [Dandelion](https://github.com/XJTU-Graphics/dandelion) 中的学习经历。Iris（鸢尾花）延续植物命名，也呼应视觉、成像与光谱的探索。

![Iris 最新编辑器：场景层级、实时视口、对象属性与资源浏览器](assets/readme/iris-editor-scene-1600x900.png)

*1600×900 工作区：Hierarchy 显示 9 个实体，Scene 预览室内模型，Inspector 展示 Object 属性，Project 浏览内置场景。[截图复现](assets/readme/README.md)*

![Iris GLSL 云海：复用 03 的清澈海面与云影](assets/readme/glsl-ocean-hero.png)

*GLSL：Thomas / @Thomas_ensc（Enscape Cube），海面：Alexander Alekseev / TDM（Seascape）；CC BY-NC-SA 3.0，见第三方许可目录。*

*M2-D 固定海面机位，1280×720：原生 Gerstner 海面、多尺度短波法线、体积云和有限云反射。该画面来自 Iris Raster，不是独立研究 Shader 或 CPU PT。[来源与复现](assets/readme/README.md)*

## 主要能力

- **实时渲染**：OpenGL 3.3、PBR / IBL、Forward / Hybrid Deferred、阴影、SSAO、TAA 与 Toon 风格。
- **离线渲染**：CPU 渐进式路径追踪、BVH / BLAS-TLAS、MIS、自适应采样、AOV，以及 PNG / HDR / EXR 输出。
- **光与自然场景**：棱镜光谱、色散、玻璃与焦散，解析大气、体积云和海面。
- **场景与资产**：OBJ、DAE、glTF / GLB 导入，场景层级、材质编辑、骨骼动画及资源浏览。
- **统一工作流**：共享 Scene / Camera、Timeline、可复现的 C++ 模块、Render Job 与持久化 Render Queue。

<details>
<summary>更多预览：海面与光照实验</summary>

![原生云海昼夜展示](assets/readme/m2d-native-reel.gif)

*24 个源帧按 24 FPS / Seed 20261006 输出；GIF 为放慢并在三个构图停留的展示版，不用于证明实时帧率。*

![Iris 海面实验：Renderer 属性与实时海面预览](assets/readme/iris-editor-1600x900.png)

*1600×900 工作区，Inspector 的 Renderer 页展示材质、PBR 与光照参数；Scene 显示程序化海面、天空和黄色方块。[截图复现](assets/readme/README.md)*

![玻璃焦散：独立光空间修复后的 Light-space 与透射阴影](assets/readme/m1-release-glass.png)

*当前 Glass3 固定机位、4× MSAA；与关闭焦散、Projector 和 Debug 的对照由 Glass3 回归覆盖。*

![棱镜光谱：21 个连续波长采样](assets/readme/m1-release-prism.png)

*Prism5 固定输入，21 个波长采样；关闭色散与七色模式分别保留为回归对照。*

![参考体积场景：Raster、CPU PT 与差异](assets/readme/m1-release-reference.png)

*256×256、512 SPP、Depth 8、Seed 20260915；差异图用于说明两条算法路径的边界，不要求像素一致。[来源与复现](assets/readme/README.md)*

</details>

## 快速开始

推荐使用 Windows、Visual Studio 2022 和支持 OpenGL 3.3 的显卡；另需 CMake 3.20+、Git 和 Python 3。首次配置会下载锁定版本的依赖。

```powershell
git clone https://github.com/Azar233/Iris.git
cd Iris
python -m pip install "Jinja2>=2.7,<4.0"
cmake --preset msvc
cmake --build --preset msvc --parallel
.\build-ci-msvc\Release\Iris.exe
```

启动后，通过 `File > Open bundled scene` 打开体积云、海面或光照实验场景。右键拖动旋转相机，中键平移，滚轮缩放；点击视口后可用 `W/A/S/D` 移动。

当前作品入口为 `01_ocean_clouds_hero`（GLSL 云海，默认隐藏研究方块）与 `03_enscape_ocean_study`（保留方块的原研究场景）。01 直接复用 03 的四 Pass GLSL 和海面参数，持续实时播放，右键旋转、中键平移、滚轮缩放。右侧 Renderer → Lighting & environment 的 GLSL 控件调节波高、频率、波形、速度、云量、反射、太阳、曝光和方块显示；普通 Water surface / Cloud layer 参数在此模式下禁用。

右侧“稳定反射 / 减少噪点”默认在 01 开启，抑制近景彩色颗粒与尖锐高光；03 默认关闭以保留原研究效果。多方向云反射和局部水面像素过滤会增加 GPU 开销，并使细小亮点略柔和。RTX 4060 Laptop、1280×720 默认实时连续 600 帧的 GPU P95 为 7.45 ms；这是本机测量，不代表所有硬件。

GLSL 海面来自 Thomas / @Thomas_ensc 的 Enscape Cube，海面过程署名 Alexander Alekseev / TDM 的 Seascape；对应 GLSL、适配修改与展示图保留 CC BY-NC-SA 3.0 许可，见 `shaders/third_party/enscape_cube/LICENSE.md`。这条路径的程序波面不等于原生 CPU/GPU 水面查询，也不替代原生云层的验收。

原生云海保留在 `assets/scenes/fixtures/28_native_ocean_clouds.myscene`，`06_ocean_clouds_hero.renderjob` 继续验证原生昼夜序列；新 `07_glsl_ocean_clouds.renderjob` 输出 0～23 帧 / 24 FPS 的 GLSL 海浪短序列。24 帧只限定导出任务，实时浏览不受此限制。旧 Cloud Lab 与 Ocean Weather Hero 可见入口已移除。原生天空 CPU 重建并行优化与自然场景合同继续保留。

```powershell
.\build-ci-msvc\Release\Iris.exe assets/scenes/01_ocean_clouds_hero.myscene
.\build-ci-msvc\Release\Iris.exe raster-sequence assets/renderjobs/07_glsl_ocean_clouds.renderjob --output ".local/renders/hero/frame_{frame:04}"
```

### 使用 Windows Release ZIP

将 `Iris-0.1.0-Windows-x64-M2D.zip` 解压到可写目录，在含 `Iris.exe` 的目录运行以下命令。ZIP 包含 shader、示例资产、Job、许可文件和 MSVC Release 运行库；运行时不需要源码、CMake、Python 或联网下载。

最低功能要求为 Windows x64、支持 OpenGL 3.3 Core 的显卡与驱动；本次验收使用 Windows 11、RTX 4060 Laptop 和 NVIDIA 591.44。更低性能硬件尚未实测，云海 High 与 CPU PT 的速度取决于硬件。CPU Batch 不创建 OpenGL 上下文，编辑器与 Raster 输出需要图形驱动。

```powershell
.\Iris.exe assets/scenes/fixtures/26_module_workflow.myscene
.\IrisBatch.exe render-sequence assets/renderjobs/01_cpu_reference.renderjob --output ".local/renders/cpu/frame_{frame:04}"
.\Iris.exe raster-sequence assets/renderjobs/04_coastal_sequence.renderjob --output ".local/renders/coastal/frame_{frame:04}"
```

示例 Job 保留源码工作流的默认输出路径；解压包推荐使用 `--output` 指定结果目录。CPU 示例输出 2 帧及 AOV，海面示例输出 13 帧 Beauty PNG；格式、缓存和 Queue 边界见下方支持矩阵。Vulkan/GPU PT、PhysX 和曲面细分尚未实现；CPU PT 尚未完整支持原生水面与空中透视，原生云海已完成云形与高度照明切片，海面与整幅作品的最终视觉定稿仍在后续阶段。

批量渲染与验证：

```powershell
.\build-ci-msvc\Release\IrisBatch.exe render-sequence assets/renderjobs/01_cpu_reference.renderjob
ctest --preset msvc
```

Content Browser 提供 ShaderScenes/Shaders 分类。选中包可在 Inspector 浏览声明的源码、include、纹理与许可文件，查看 Pass/Channel 归属和缺失状态；Refresh 更新依赖快照。可打开包、外部文本编辑器和所在目录，并对活跃包执行完整事务式重新编译：失败保留运行实例，成功重置历史。编译诊断标明 Pass 与原始文件/行，可选择错误查看源码上下文；生成的 mainImage 包装器使用独立来源标记，未知日志保留原文。Inspector 根据运行包生成 float/bool 控件，支持默认值恢复与 Scene 覆盖值保存；展示 Pass/Channel、实际帧号和历史状态，并可清空反馈历史。启用 Shader hot reload 时，Manifest、GLSL/include 和 PNG/raw 纹理变化自动准备并发布完整包；失败保留配置、像素与反馈，缺失依赖修复后自动恢复，也可显式重试。History policy 可保存参数/相机变化时是否清空反馈和最大正向时间步（0.001～10 s）；默认 true/true/0.25 s，策略修改、resize、包替换、倒退和 Clear 始终重置，保留反馈没有运动重投影。停用热重载期间推迟文件更新。监听采用 mtime/大小；保留两者的内容修改需手动 Recompile。成功打开的独立包会把声明的源码/include/纹理注册到当前会话的资源浏览器；Refresh 更新缺失/恢复状态，同名包按路径区分。

目录、Iris 状态迁移和按领域的最小验证入口见 [项目工作区说明](WORKSPACE.md)。普通开发 CI 构建与 CPU 检查，发布打包单独触发。

## 工作流与 Job 支持范围

File 菜单、快捷键与资源浏览器通过共享命令执行新建、保存、场景打开与模型导入；窗口可直接接收 `.myscene`、`.renderjob` 和模型文件拖放。View 菜单的五个展示 Preset 打开对应的完整场景。CPU Settings 的设置变化取消旧任务；暂停时 Restart 重算当前帧并保持暂停。

| 能力 | CPU Job / IrisBatch / GUI Queue | Raster Job / Iris raster-sequence |
| --- | --- | --- |
| 格式 | PNG、Radiance HDR、OpenEXR，可组合 | Beauty PNG |
| AOV | Beauty、Albedo、Normal、Depth、Direct、Indirect、Sample Count、Variance | Beauty |
| Resume | 按已验证输出报告恢复完整帧 | 不支持，执行前拒绝 |
| Simulation Cache | 显式配置 Module 的 Job；Hit/Missing/Stale 均有诊断 | 不支持，执行前拒绝 |
| GUI Queue | 支持提交、取消、重试与恢复 | schema 4 ShaderScene 序列支持；原生 Raster 仍使用 CLI |
| Module 参数 | `.renderjob.module` 明确定义并优先于 Scene 配置 | `.renderjob.module` 明确定义 |

通用 ShaderScene 序列通过 schema 4 的 `shaderScene` 配置显式启用，见 [最小反馈示例 Job](assets/renderjobs/08_shader_scene_sequence.renderjob)。`history: "reset-at-start"` 在开始时清空反馈，随后按帧顺序执行；`warmupFrames` 是起始帧之前的固定时间步，不是旧 Raster 的同一时刻重复渲染。时间采用共享 Timeline 的绝对 `frame / fps`，从第 0 帧开始且有预热时会产生负的预热时间。`sampling.seed` 传入可选 GLSL `int irisSeed`，声明纹理的 Seed 保持原值。更改 Seed 始终清空反馈。每帧生成 PNG 和输入身份报告，包含完整源码/include、纹理和运行二进制指纹、有效参数、API/Build ID、时间和历史策略。CLI 与 GUI Queue 使用同一个 Raster 执行入口；Queue 在独立上下文进程中执行，取消在输出边界生效。当前 Queue 进程执行器限 Windows。

ShaderScene 序列仅支持 Beauty PNG，禁用热重载，拒绝 Resume、Cache、Module、AOV/HDR 和继续失败策略；限制 10000 帧及 240 帧预热，FPS/范围还须满足 Scene 的历史时间步策略。所有输出碰撞在执行前拒绝。旧 schema 1～3 的原生/Enscape Raster 时间与逐输出重置保持兼容，通用 ShaderScene 仍须显式启用 schema 4。GUI 自由播放/任意跳帧不会自动重放此前的反馈；要复现输出，应使用相同初始状态、Seed、预热和顺序。

[Enscape 共享序列 Job](assets/renderjobs/09_enscape_shader_sequence.renderjob) 使用相同 schema 4，包及输入、许可随 Release ZIP 安装。自制包使用 MIT，Enscape 包保留 CC BY-NC-SA 3.0 及原作者说明。报告另记录 Pass/Channel 格式与采样、纹理尺寸/编码/Seed 和许可；Queue 失败显示执行器退出码与有界日志（最多 2 KiB 开头和 14 KiB 尾部，中间截断有标记），修复输入后可重新提交。GUI Queue 执行限 Windows；CLI 需要可用的 OpenGL 3.3 上下文。跨驱动像素一致性未作保证。

```powershell
.\build-ci-msvc\Release\Iris.exe raster-sequence assets/renderjobs/08_shader_scene_sequence.renderjob
.\tools\ShaderSceneSequenceAcceptance.ps1
.\tools\ShaderSceneDeliveryAcceptance.ps1
```

`.myscene.module` 用于编辑器保存和恢复。CPU Batch 不自动继承这份模块配置：带 Module 的 Scene 必须搭配显式配置 Module 的 Job，避免保存配置被静默忽略。Scene 保存的是参数与 Seed，执行范围和 FPS 由 Timeline / Job 明确定义；示例采用 0～23 帧、24 FPS。

固定示例：[模块场景](assets/scenes/fixtures/26_module_workflow.myscene)与[对应 Job](assets/renderjobs/05_module_workflow.renderjob)，使用 Turntable、22.5 度/帧、Z 轴和 Seed 20260919。可在 GUI 保存重开并预览，再由 Batch 输出；自动验收核对两者的第 12 帧图像、参数、FPS、Seed、API 与 Build ID。

```powershell
.\build-ci-msvc\Release\Iris.exe assets/scenes/fixtures/26_module_workflow.myscene
.\build-ci-msvc\Release\IrisBatch.exe render-frame assets/renderjobs/05_module_workflow.renderjob 12
cmake --build build --config Release --target workflow-acceptance
```

## 构建与测试

MinGW 用户可将配置命令改为 `cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`，然后运行 `cmake --build build-mingw --parallel`。

以下目标分别验证真实 OpenGL 启动、运行固定图回归和生成可独立运行的 ZIP：

```powershell
cmake --build build --config Release --target gpu-smoke
cmake --build build --config Release --target renderer-regression-suite
cmake --build build --config Release --target package
```

## 项目结构

```text
src/       编辑器、场景、实时渲染、路径追踪、光学与运行时
shaders/   GLSL 渲染与后处理
assets/    模型、场景、环境、图标与 Render Job
tests/     自动化测试
tools/     资产生成、验收、截图与性能工具
```

Iris 仍在持续开发，实时与离线路径的支持范围并不完全相同。内部 CMake target（`MyRenderer`、`MyRendererBatch`）、`MYRENDERER_*` 环境变量和持久化标识保留旧名称，以兼容已有项目。

## 许可与致谢

项目采用 [MIT License](LICENSE)。感谢 Dandelion 提供最初的图形学学习土壤，以及 GLFW、GLAD、GLM、Dear ImGui、Assimp 等开源项目；第三方代码和资产的许可分别见[依赖声明](THIRD_PARTY_NOTICES.md)与[资产来源](ASSET_LICENSES.md)。
