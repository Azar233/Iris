# P0-C GUI CPU 渐进式参考预览（CPU Progressive Preview）

- 完成日期：2026-09-17（正文）；`docs/media/` 工作区插图与 CPU 预览导出采集于 2026-09-20
- 源码 revision：`35a726c`（工作区含未提交改动，本文档与插图尚未入库）
- 构建目录：`build-ci-msvc`，Visual Studio 17 2022，Release，`BUILD_TESTING=ON`
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0

## 目标与范围

P0-C 把现有 `SceneSnapshot`、`ProgressiveRenderer` 与 `RenderTask` 接入编辑器 Viewport，不增加第二套场景、相机或资产加载路径。它是可持续更新的 CPU 参考预览，不承诺交互式实时帧率：交互期间降分辨率、停止操作后逐步升档，最终结果与 CLI 参考输出一致。

本阶段明确不做的事：

- 不新建场景加载器、相机或资产导入路径，也不改写 `.myscene`：预览参数（Target SPP、Max Depth、Seed、AOV、Preview resolution）是 `Application` 的任务状态，不属于 `RendererSettings` 快照，因此不随场景持久化。
- 不把 Raster 专属的屏幕空间效果带进参考图，见「限制与取舍」。
- 不承诺 CPU 侧实时帧率，也不承诺动态场景的收敛（持续动画会不断让快照失效）。

## 实现

### 运行时：快照、任务代次与线程合同

- 编辑器主线程捕获不可变 `SceneSnapshot`；CPU worker 持有自己的快照并运行现有 Tile 线程池。
- `RenderTask` 的每次 `start()` 都获得单调递增 task ID。发布前再次核对当前 ID，因此已取消任务不能覆盖较新的任务或纹理。
- Worker 最多每 100 ms 发布一次不可变 `RenderProgress`，其中包含完整 `RenderImage`、统计和显示就绪的 bottom-up sRGB RGBA8 staging image。GUI 每帧只取得共享只读指针，不深拷贝 Beauty+AOV。
- OpenGL Texture 的创建、更新和删除全部在拥有 Context 的主线程执行。失效后旧 texture 不再作为当前 CPU 图像显示；退出时先 cancel/join，再销毁 GL 资源和 Scene/GPU 资产。
- 输入签名覆盖 Scene generation、实体 ID/父子关系/模型、世界 Transform、Tint、显隐/阴影、Camera orbit/FOV、方向光、局部光、环境、Viewport 尺寸、SPP、Depth、Seed、AOV 和比例。任何变化都会先让旧 task ID 失效并请求取消；连续拖动采用 75 ms debounce，避免每个 GUI frame 同步重建 BVH。

### UI：Viewport 的 Render Mode、CPU Settings 与统计 Overlay

1. 在 Viewport 顶部 Render Mode 选择 `CPU Path Traced`。
2. 打开 `CPU Settings` 设置 Target SPP、Max Depth、Seed、AOV 和预览比例。
3. `Auto` 在输入稳定 75 ms 后从 1/4 分辨率开始，稳定 450 ms 后升到 1/2，稳定 1200 ms 后进入全分辨率。固定的 1/4、1/2、Full 不自动升档。
4. `Pause` 在完整 SPP pass 边界暂停，不提交半轮结果；`Resume` 继续当前任务。`Restart` 清空累积并从相同输入重新渲染。
5. `Export` 把当前已发布的 Beauty 保存为 Reinhard+sRGB PNG 和线性 RGBE HDR，并同时导出 Albedo、Normal、Depth、Direct、Indirect、Sample Count、Variance 七组 AOV。

预览分辨率是 Viewport 尺寸除以档位除数（`Auto` 按稳定时长取 4 / 2 / 1，固定档位取 4 / 2 / 1），所以同一份场景在不同窗口尺寸下导出的 PNG 尺寸也随之变化。

Overlay 不只使用颜色表达状态，会明确显示 Rendering、Paused、Restarting、Complete 或 Failed，以及当前/目标 SPP、百分比、预览/Viewport 尺寸、Seed、Depth、累计时间、Path/Shadow Ray 和 BVH Bounds/Triangle Tests。

### 诊断：导出路径与 Log / Profile

`Export` 写入 `nextScreenshotPath()` 决定的 stem（PNG + HDR + 七组 AOV，若该帧有降噪结果还会额外写出 `-denoised`）；无人交互时可以用 `MYRENDERER_CPU_PREVIEW_EXPORT` 指定 stem，让预览跑到目标 SPP 后写出与 CLI 完全相同的参考 PNG 并自动退出，这是 GUI 帧与 Batch/CLI 帧逐字节比较的入口。底部工作区的 Log / Profile 标签页提供同场景的运行时读数（CPU/GPU 帧时间、Draw Call、三角形、活动 Pass 与逐 Pass GPU 时间），便于把一次预览与一次 CLI 输出对照。

## 截图

工作区在 1440×900 默认 Dock 下的布局：中央 Viewport 顶部是 Render Mode 与任务按钮，右侧 Inspector，底部多标签 Workspace。CPU Preview 的两个入口——Viewport 顶部 Render Mode 与底部 Log / Profile——都在这套布局里可达。

![默认 Dock 工作区（1440×900）：Viewport 顶部 Render Mode、右侧 Inspector 与底部多标签 Workspace](media/p1-workspace-1440x900.png)

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_EDITOR_WINDOW_WIDTH='1440'; $env:MYRENDERER_EDITOR_WINDOW_HEIGHT='900'
$env:MYRENDERER_EDITOR_SCREENSHOT='docs/media/p1-workspace-1440x900.png'
build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/18_atmosphere_sky.myscene
```

同一工作区在 1100×680 应用下限下 Viewport、Inspector 与底部标签仍可达，说明预览所需的控件不依赖 1440×900 的宽窗口。

![同一工作区在 1100×680 应用下限下的布局与可达性](media/p1-workspace-1100x680.png)

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_EDITOR_WINDOW_WIDTH='1100'; $env:MYRENDERER_EDITOR_WINDOW_HEIGHT='680'
$env:MYRENDERER_EDITOR_SCREENSHOT='docs/media/p1-workspace-1100x680.png'
build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/18_atmosphere_sky.myscene
```

Log / Profile 标签页是 CPU 预览的对照面：`Render tasks` 分组在没有提交任务时给出显式空状态 `No Render Job has been submitted.`，`Runtime profile` 把 CPU/GPU 帧时间、Draw Call、三角形与活动 Pass 放在一起。图中 `CPU frame: 649.02 ms` 与 `GPU frame: 512.376 ms` 是该次采集的瞬时读数，不是基准结论。

![Log / Profile 标签页（1440×900）：Render tasks 空状态与 Runtime profile 汇总读数](media/p1-workspace-log-profile.png)

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_EDITOR_WINDOW_WIDTH='1440'; $env:MYRENDERER_EDITOR_WINDOW_HEIGHT='900'
$env:MYRENDERER_EDITOR_SCREENSHOT_TAB='log'
$env:MYRENDERER_EDITOR_SCREENSHOT='docs/media/p1-workspace-log-profile.png'
build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/18_atmosphere_sky.myscene
```

上面三张工作区截图是在 `Raster | Edit` 状态下采集的，因此它们证明的是布局、标签页与最小窗口下的可达性，而不是 CPU Path Traced 的运行状态；CPU 状态本身由下一张导出图与 Overlay 文案证明。

CPU Path Traced 预览在同一外景夹具上的导出结果：天空渐变、太阳盘与地面阴影的构图与 Raster 一致，地面与柱体上密布的亮点是低 SPP 下的 firefly 噪声，因此这张图证明的是「预览走的是与 Raster 相同的统一太阳与环境」，而不是收敛后的画质。该 PNG 只写入构建目录、不入库，所以本文只给路径与复现命令。

路径：`build-ci-msvc/p1a-cpu-preview-vs-raster.png`（846×525 单帧 CPU 结果；文件名里的 `vs-raster` 只是导出 stem，`MYRENDERER_CPU_PREVIEW_EXPORT` 不会附带 Raster 对照或 AOV）。

```powershell
$env:MYRENDERER_CPU_PREVIEW='1'          # 1 = CPU Path Traced
$env:MYRENDERER_CPU_PREVIEW_SCALE='3'    # 0 Auto / 1 1/4 / 2 1/2 / 3 Full
$env:MYRENDERER_CPU_PREVIEW_SPP='64'
$env:MYRENDERER_CPU_PREVIEW_DEPTH='6'
$env:MYRENDERER_CPU_PREVIEW_SEED='1'
$env:MYRENDERER_CPU_PREVIEW_EXPORT='build-ci-msvc/p1a-cpu-preview-vs-raster'
build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/18_atmosphere_sky.myscene
```

> **待复核**：记录下来的那次导出没有留下 SPP / Depth / Seed / Preview resolution，因此上面的命令给的是可执行的同类复现（导出分辨率随 Viewport 尺寸与档位变化），不保证与仓库里这张 846×525 PNG 逐像素一致。

## 验证

GUI staging 与 `writeReferenceImage()` 共用 `makeDisplayRgba8BottomUp()`：相同 SceneSnapshot、分辨率、SPP、Depth 和 Seed 使用同一积分器、Reinhard 映射、sRGB 编码和像素取整。`path-tracing-progressive` 的专项测试覆盖：

- 同步 `ProgressiveRenderer`、后台 `RenderTask` 与 GUI staging 的逐值一致；
- 快速 long-task → replacement-task 重启后，只能发布 replacement task ID；
- GUI bottom-up RGBA staging 翻转到文件行序后，与 CLI PNG RGB 字节逐位一致；
- 取消只保留完整 SPP pass，析构会 cancel/join。

推荐验证入口：

```powershell
cmake --build build-ci-msvc --config Release --target MyRendererProgressiveTests MyRenderer
ctest --test-dir build-ci-msvc -C Release -R path-tracing-progressive --output-on-failure
cmake --build build-ci-msvc --config Release --target gpu-smoke
```

## 限制与取舍

- CPU Preview 当前使用 CPU Path Tracer 已支持的材质/光照语义；Raster-only 的屏幕空间效果、Stylized、Grid、Axes、Selection Outline 和实时后处理不会混入参考图。
- 动画或持续自动旋转会持续使快照失效，停止交互后才开始新的渐进累积；因此本文不给出动态场景的收敛数字。
- 预览参数不在 `.myscene` 里：它属于 `Application` 的任务状态，只能通过 `CPU Settings` 弹窗或 `MYRENDERER_CPU_PREVIEW*` 覆盖项设置，重开场景不会恢复上一次的 SPP/Depth/Seed。
- 本文的工作区截图取自 `Raster | Edit` 状态，只能作为布局与标签页证据；CPU 预览的 Overlay 文案本身没有本次采集的截图，需要按「复现命令」重拍。
- CPU 预览导出的 SPP/Seed/档位没有随产物一起记录，`docs/media/` 之外的那张导出图由构建目录路径引用，不入库，也不参与任何逐像素比对。
- 逐值一致与逐字节一致的结论都在同一构建目录、同一台参考机上取得，本文没有跨机器证据。
- GUI 与 CLI 的采样器默认值不同：编辑器默认走 P0-D 的采样改进（Power-weighted lights、GGX VNDF），CLI 默认是 Uniform light selection 与 GGX NDF。因此要让 GUI 帧与 Batch 帧逐字节一致，除了 SPP/Depth/Seed，还要用 `MYRENDERER_CPU_PREVIEW_POWER_LIGHTS` 与 `MYRENDERER_CPU_PREVIEW_VNDF` 把两侧对齐。

## 复现命令

```powershell
# 构建 + 专项测试
cmake --build build-ci-msvc --config Release --target MyRendererProgressiveTests MyRenderer
ctest --test-dir build-ci-msvc -C Release -R path-tracing-progressive --output-on-failure

# CPU Path Traced 预览导出（跑到目标 SPP 后写出 PNG 并退出）
$env:MYRENDERER_CPU_PREVIEW='1'
$env:MYRENDERER_CPU_PREVIEW_SCALE='3'
$env:MYRENDERER_CPU_PREVIEW_SPP='64'
$env:MYRENDERER_CPU_PREVIEW_DEPTH='6'
$env:MYRENDERER_CPU_PREVIEW_SEED='1'
$env:MYRENDERER_CPU_PREVIEW_EXPORT='build-ci-msvc/p1a-cpu-preview-vs-raster'
build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/18_atmosphere_sky.myscene

# 工作区与 Log / Profile 插图重拍
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_EDITOR_WINDOW_WIDTH='1440'; $env:MYRENDERER_EDITOR_WINDOW_HEIGHT='900'
$env:MYRENDERER_EDITOR_SCREENSHOT_TAB='log'
$env:MYRENDERER_EDITOR_SCREENSHOT='docs/media/p1-workspace-log-profile.png'
build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/18_atmosphere_sky.myscene
```

其他可用的预览覆盖项见 `src/app/Application.cpp` 的 `MYRENDERER_CPU_PREVIEW*` 读取段（`_AOV`、`_POWER_LIGHTS`、`_VNDF`、`_DENOISE`）；常规编辑器操作仍走 Viewport 顶部的 Render Mode、`CPU Settings` 与 `Pause` / `Resume` / `Restart` / `Export`。

## 下一步

- `todolist.md` 的 `P1-0A Workspace` 一节里，待办条目「真实图像缩略图、Simulation/Module Parameters、Modules/Log 操作、GPU PT Overlay、场景 Preset 构造动作与 CPU Preview 任务参数的命令化」仍未勾选：CPU Preview 的任务参数弹窗是当前少数仍直接写入界面的表面之一，下一步把它收口到 `EditorCommand` 与 `EditorDomain`，与其余 Renderer 分组一致。
- 同一节的 `GPU PT Overlay` 与 `Modules/Log 操作` 属于后续切片；本文只描述 CPU 侧的渐进预览。
- 预览的天空/光照现在与 Raster 共用 `src/optics/Atmosphere.*` 的统一太阳，该模型的实现与限制见 [`atmosphere-sky.md`](atmosphere-sky.md)。
