# GP-P1A：Hybrid Deferred Shading（混合延迟着色）基线

- 记录日期：2026-08-25
- 源码 revision：`e558f17`（`feat: add hybrid deferred shading baseline`，本阶段的实现与本文同批入库）
- 构建目录：性能数字与原始 JSON 来自 `build-release`（`build-release/deferred-benchmarks/` 下的四份 JSON）；本文命令沿用原文的 `build-release`
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0；这与 `docs/regression-baseline-audit.md` 记录的审计环境、以及 Benchmark JSON 的 `gpu` 字段 `NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 | OpenGL 3.3.0 NVIDIA 591.44` 一致
- 测量环境：1920×1080，30 帧预热 + 90 帧测量

## 目标与范围

本阶段为 Iris 增加一条可在运行时切换的 Deferred Shading（延迟着色）路径，同时保留原有 Forward（前向着色）路径作为同机、同场景、同镜头的画质与性能对照。实现采用 **Hybrid Deferred（混合延迟着色）**：不透明物体进入 G-Buffer（几何缓冲）和全屏 Lighting Pass（光照阶段）；需要排序、折射和体积吸收的透明/玻璃材质继续进入现有 Forward Transparent Pass（前向透明阶段）。

这样切分的原因是透明材质不能按普通不透明 G-Buffer 的方式合成，所以路径才叫 Hybrid 而不是纯 Deferred；两条路径后续共用透明折射与后处理，因此 Glass-4 的双界面折射、色散和焦散能力得以保留。Forward 模式不分解 Geometry/Lighting，仍由原来的 `Forward opaque HDR scene` 一次完成不透明着色。

本阶段明确不做的事记在「限制与取舍」：只有单方向光，没有多点光/聚光与 Light Volume；G-Buffer 保存世界空间法线，没有 Octahedral Normal Encoding（八面体法线编码）；4× MSAA 下 MRT（multiple render targets，多渲染目标）的成本明显，没有按像素着色、边缘着色或 TAA 替代方案；屏幕空间世界位置重建仍依赖当前深度精度，没有 Reversed-Z（反向 Z）。

## 实现

### 持久化：`.myscene` 里的 `renderPath` 与 `gBufferDebugView`

`RendererSettings::renderPath` 决定这一次绘制走 Forward 还是 Hybrid Deferred，`gBufferDebugView` 决定逐附件调试视图显示哪一张。两者与同组的 `shadingMode` 一起写入 `.myscene` 的 renderer 设置（`src/scene/SceneDocument.cpp` 中的 `renderPath`、`shadingMode`、`gBufferDebugView` 三个键），旧文件缺少字段时分别取各自的默认值。因此「用哪条路径、看哪个附件」是场景文件里的状态，固定回归不需要额外传参。

### 运行时：`GBuffer` 的生命周期

G-Buffer 由 `GBuffer` RAII 对象持有，附件尺寸或采样数变化时统一重建：`resize(width, height, samples)` 先把采样数钳制到 `GL_MAX_SAMPLES` 以内，尺寸与采样数都没变时直接返回，否则重建全部附件。`Renderer::render` 只在 `renderPath == Deferred` 时调用它，退回 Forward 时会 `destroy()` 已分配的 MRT，避免保持一份不再使用的显存——注释里写明这是为了让 GUI 与 Benchmark 的显存对照保持诚实。

### 渲染：Pass 顺序

```text
Shadow map / Transmission shadow / Caustics
                    |
                    v
        G-buffer geometry (opaque MRT)
                    |
          resolve color + depth
                    |
                    v
 Deferred lighting (depth reconstruction + PBR/IBL)
                    |
       Opaque HDR + generated mip chain
                    |
                    v
 Forward transparent / refractive scene
                    |
                    v
             Bloom + tone map
```

### 渲染：G-Buffer 布局

| Attachment | OpenGL 格式 | 内容 | 每像素 |
| --- | --- | --- | ---: |
| Albedo | `GL_RGBA8` | 线性基础色；Alpha 保留材质覆盖率 | 4 B |
| Encoded Normal | `GL_RGBA16F` | 世界空间法线编码为 `normal * 0.5 + 0.5` | 8 B |
| Metallic / Roughness | `GL_RG8` | R=Metallic，G=Roughness | 2 B |
| Depth / Stencil | `GL_DEPTH24_STENCIL8` | 世界位置重建、深度测试与后续透明遮挡 | 4 B |

Resolved G-Buffer 合计 18 B/px。4× MSAA 同时保留一套 4 倍采样 Renderbuffer，因此 1920×1080 时相对 Forward 额外占用 186,624,000 B（约 178.0 MiB）。附件尺寸、采样数变化时由 `GBuffer` RAII 对象统一重建并计入 Renderer 的显存估算。

### 渲染：Lighting Pass

全屏三角形从 Depth 重建世界坐标，从 Encoded Normal 恢复单位法线，并使用与 Forward 路径对应的 Cook-Torrance GGX 直接光、Split-Sum IBL、方向光 PCF Shadow、彩色 Transmission Shadow 与 Caustics。最终模式在无几何像素处 `discard`，保留先绘制的 HDR 天空盒。

逐附件调试模式直接显示原始 MRT 数据。为避免误判，启用 Albedo、Normal、Metallic/Roughness 或 Depth 调试时会自动跳过天空盒、网格/坐标轴、透明折射、光路 Overlay、Bloom 和 Tone Mapping。

### UI：Inspector 的 `Opaque render path` 与 `G-buffer debug`

在 `Inspector → Renderer → PBR & environment` 中：

1. 将 `Opaque render path` 从 `Forward` 切换到 `Deferred (hybrid)`。
2. 使用 `G-buffer debug` 选择 `Final lighting`、`Albedo`、`Encoded normal`、`Metallic / Roughness` 或 `Depth`。
3. 在 `Final lighting` 下切换 PBR、IBL、Shadows、1×/4× MSAA，观察 Geometry Pass 与 Lighting Pass 的活动时间。
4. 加载玻璃模型时，透明物仍由 Forward Transparent Pass 绘制；这是混合路径的预期行为，不是漏写 G-Buffer。

同样的状态可通过 `MYRENDERER_RENDER_PATH=0|1` 与 `MYRENDERER_GBUFFER_DEBUG=0..4` 自动化。`MYRENDERER_GRID`、`MYRENDERER_AXES`、`MYRENDERER_GROUND` 用于固定回归舞台。

### 诊断：逐附件调试视图与 GPU Pass 时间

逐附件视图是本阶段唯一的画面级诊断口：它把 Lighting Pass 真正读到的 MRT 内容直接显示出来，并在启用时自动旁路天空盒、Overlay、透明与后处理，因此看到的是 attachment 本身，而不是最终合成结果。

时间侧的证据来自 Benchmark JSON：`deferred-benchmark` 把活动 Pass 的 GPU P50/P95、Draw Call 以及 RenderTarget / 纹理 / 几何显存估算写进构建目录，下表「主要不透明 Pass P50」一列即取自其中的逐 Pass 条目（JSON 里的 Pass 名是 `Forward opaque HDR scene`、`G-buffer geometry` 与 `Deferred lighting`）。

## 截图

六张图都由 `deferred-visual-regression` 采集：同一资产 `assets/models/pbr_material_test.gltf`、同一机位、同一 HDRI、同一方向光、1920×1080 与 4× MSAA，写入构建目录 `deferred-visual-current/` 后与 `docs/images/` 的基线逐像素比对。前两张是两条路径的最终画面，后四张是 G-Buffer 的逐附件原始视图；每张图的路径开关见「复现命令」的对照表。

### Forward / Deferred 同机位一致性

同一资产、同一机位、同一 HDRI、同一方向光与 4× MSAA 下，两张最终图的归一化 RGBA MAE 只有 `0.000517`、明显变化像素 `0.135%`。这张对照证明的是**两条路径渲染同一个场景得到同一个结果**：Hybrid Deferred 没有改变不透明着色，因此后续出现的任何画质差异都可以归因于路径本身，而不是夹具、镜头或光照。

| Forward | Deferred |
| --- | --- |
| ![Forward 最终画面：同机位对照的左栏](images/gp_p1_forward_final.png) | ![Deferred 最终画面：同一夹具与机位下与 Forward 的 MAE 为 0.000517，证明两条路径一致](images/gp_p1_deferred_final.png) |

### G-Buffer 逐附件原始视图

这四张图把 Albedo、Encoded Normal、Metallic/Roughness 与 Depth 四个 attachment 直接显示出来，绕过了天空盒、网格/坐标轴、透明折射、光路 Overlay、Bloom 与 Tone Mapping。它们证明的是 **Lighting Pass 的输入本身**：Albedo 是线性基础色与材质覆盖率、Encoded Normal 是世界空间法线（`normal * 0.5 + 0.5`）、Metallic/Roughness 是两个标量通道、Depth 是世界位置重建与后续透明遮挡的依据。天空方向没有不透明几何写入，所以这些 attachment 在天空区域只有清屏值（Albedo 与 Depth 视图里表现为黑色，Encoded Normal 视图里是一块平坦的编码值）；最终模式在同一批像素上 `discard`，天空改由先绘制的 HDR 天空盒提供。

| Albedo | Encoded Normal |
| --- | --- |
| ![G-Buffer Albedo 原始 attachment：线性基础色与 Alpha 覆盖率，天空区域为空](images/gp_p1_gbuffer_albedo.png) | ![G-Buffer Encoded Normal 原始 attachment：世界空间法线按 normal * 0.5 + 0.5 编码](images/gp_p1_gbuffer_normal.png) |

| Metallic / Roughness | Depth |
| --- | --- |
| ![G-Buffer Metallic / Roughness 原始 attachment：R=Metallic、G=Roughness 两个标量通道](images/gp_p1_gbuffer_material.png) | ![G-Buffer Depth 原始 attachment：世界位置重建、深度测试与透明遮挡的依据](images/gp_p1_gbuffer_depth.png) |

复现这六张图的 target：

```powershell
cmake --build build-release --config Release --target deferred-visual-regression
```

## 验证

- `deferred-visual-regression` 是画质验收口：按固定环境变量重拍 6 张 1920×1080 图片，写入构建目录 `deferred-visual-current/`，再用 `MyRendererImageComparison` 与 `docs/images/` 的基线逐像素比对，容差是跨驱动的 MAE `0.015` 与变化像素 `8%`；采集或比对失败时脚本报出 `FATAL_ERROR`，因此该 target 失败即非零退出。前两张图证明 Forward/Deferred 一致，后四张证明逐附件调试视图与基线一致。
- `deferred-benchmark` 是性能证据口：在同一夹具上跑 Forward / Deferred × 1× / 4× MSAA 四组，预热 30 帧、测量 90 帧，把 GPU P50/P95、活动 Pass 的 GPU 时间、Draw Call 与显存估算写进 `build-release/deferred-benchmarks/`，文件名是 `gp_p1_forward_msaa1`、`gp_p1_forward_msaa4`、`gp_p1_deferred_msaa1`、`gp_p1_deferred_msaa4`。
- `gpu-smoke` 用真实 OpenGL 上下文运行应用，其中包含 `MYRENDERER_RENDER_PATH=1` 与 `MYRENDERER_GBUFFER_DEBUG=0` 的组合（覆盖 `pbr_material_test.gltf` 与 `glass_material_test.gltf`），因此 Deferred Lighting Shader 必须真正编译链接，而不只是在单元测试里成立。
- `renderer-regression-suite` 把 Deferred 套件作为其中一支纳入整套视觉回归；`docs/regression-baseline-audit.md` 记录该套件在 Kloofendal EXR 成为新固定输入后随基线更新通过（更新前它是 Fail：`gp_p1_forward_final` MAE `0.109568`、变化 `83.1615%`；接受基线更新后十支套件全部 Pass，独立复核对每张图报告 MAE `0`、变化像素 `0%`）。
- 尚未验证：下表的时间与显存数字没有跨机器证据，表内数字与同目录现存 JSON 的差异见「限制与取舍」的「待复核」条目。

### 1920×1080 Forward / Deferred 基准（`deferred-benchmark`）

环境：NVIDIA GeForce RTX 4060 Laptop GPU，OpenGL 3.3 / 驱动 591.44，1920×1080，30 帧预热 + 90 帧测量。这里仍只有单方向光，因此结果用于建立基线，不宣称 Deferred 已带来性能收益。

| 路径 | MSAA | GPU Frame P50 / P95 | 主要不透明 Pass P50 | Draw Calls | Render Memory |
| --- | ---: | ---: | ---: | ---: | ---: |
| Forward | 1× | 1.186 / 2.352 ms | Forward Opaque 0.204 ms | 22 | 196.2 MiB |
| Deferred | 1× | 1.319 / 2.528 ms | G-Buffer 0.076 + Lighting 0.243 ms | 23 | 231.8 MiB |
| Forward | 4× | 1.435 / 2.571 ms | Forward Opaque 0.358 ms | 22 | 291.2 MiB |
| Deferred | 4× | 1.860 / 3.022 ms | G-Buffer 0.449 + Lighting 0.257 ms | 23 | 469.1 MiB |

当前简单场景中，Deferred 多一次全屏 Pass，并承担 MRT 写带宽；4× MSAA 又把所有 G-Buffer Attachment 扩为多采样存储，所以速度和显存均落后于 Forward。下一阶段会增加点光/聚光与多档光源数量，才评估 Deferred 把“几何复杂度 × 光源数量”解耦后的扩展性。

表的重拍命令：

```powershell
cmake --build build-release --config Release --target deferred-benchmark
```

## 限制与取舍

- **当前 Lighting Pass 只有单方向光**；多点光/聚光和 Light Volume 尚未进入本阶段。这是本阶段最有意的取舍：先建立 G-Buffer、深度重建与逐附件调试的可用基线，再评估「几何复杂度 × 光源数量」解耦后的扩展性。该项已由后续 GP-P1B 的多光源压力场景接手（`docs/local-light-stress.md`，64 灯下 Deferred 整帧 GPU P50 相对 Forward 约 `1.73×`），但 `todolist.md` 里仍没有 Light Volume 的独立条目。
- **G-Buffer 保存世界空间法线，便于调试但带宽较高**；后续可评估 Octahedral Normal Encoding。本文没有该编码的实测数据，`todolist.md` 中也还没有对应条目。
- **4× MSAA 对 Deferred 的 MRT 成本明显**；尚未加入按像素着色、边缘着色或 TAA 替代方案。表里 4× 一行的 G-Buffer P50（`0.449 ms`）对 Forward Opaque（`0.358 ms`）的差距就来自这里。
- **透明材质不能按普通不透明 G-Buffer 方式合成，因此继续 Forward**；这也是路径称为 Hybrid Deferred 的原因。这不是待修的缺口，而是被接受的边界。
- **屏幕空间世界位置重建依赖当前深度精度，尚未采用 Reversed-Z。** 本文没有量化这一精度损失的实验，`todolist.md` 中也还没有对应条目。
- **待复核（G-Buffer 显存口径）**：表中的 18 B/px 与「1920×1080 时额外占用 186,624,000 B（约 178.0 MiB）」是本阶段三张颜色附件加 Depth 的口径（18 B/px × 5 = 90 B/px）。当前工作树的 `GBuffer::estimatedBytes()` 已按 4+8+2+8+4 = 26 B/px 计算，因为它多了一张 Motion Attachment（`gbuffer.frag` 的 `gMotion`，输出到 COLOR_ATTACHMENT3，由后续 GP-P1D 引入）。本文按原文保留原数字，没有替原作者改写；按现码重算会得到更大的值，这一点本文没有逐项复算。
- **待复核（基准表与现存 JSON 不一致）**：上表是本阶段（2026-08-25，revision `e558f17`）的实测值；同一台参考机 `build-release/deferred-benchmarks/` 里现存四份 JSON 的写入时间是 2026-09-02，数值与表不同——Forward 1× 的 `gpuFrameP50Ms` 是 `1.181696`、`gpuFrameP95Ms` 是 `2.281472`，`G-buffer geometry` P50 是 `0.105472 ms`，`renderMemoryBytes` 是 `284508000 B`（约 271.3 MiB）。差异中可解释的一部分是上面那张 Motion Attachment 带来的显存口径变化，但整组差异本文没有复算，也没有找到一次与表内数字完全对应的重跑记录，因此保留原文数字并在此留痕。
- **性能数字只有一台参考机的证据**：`1.186 / 2.352`、`1.319 / 2.528`、`1.435 / 2.571`、`1.860 / 3.022 ms` 与 `196.2 / 231.8 / 291.2 / 469.1 MiB` 都来自同一台 NVIDIA GeForce RTX 4060 Laptop GPU（驱动 591.44，OpenGL 3.3.0），本文没有跨机器或跨驱动的对照。
- **表中的 Render Memory 是代码侧估算，不是 GPU 实测分配**：它来自 `Renderer::estimatedRenderMemoryBytes()` 对各子系统 `estimatedBytes()` 的求和（含 `GBuffer::estimatedBytes()`），不含驱动簿记与分配器填充。
- **固定回归的跨驱动容差是 MAE `0.015` 与变化像素 `8%`**，比 Forward/Deferred 之间的实际差异（`0.000517`、`0.135%`）宽得多；因此这个 target 能发现路径回归，但不能用来复核两条路径的像素级等价。

## 复现命令

```powershell
cmake --build build-release --config Release --target deferred-visual-regression
cmake --build build-release --config Release --target deferred-benchmark
cmake --build build-release --config Release --target gpu-smoke
```

`deferred-visual-regression` 使用的固定环境变量集中在 `tools/DeferredVisualRegression.cmake`；六次采集之间只有路径与调试视图不同，其余开关相同：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_MSAA='4'
$env:MYRENDERER_PBR='1'; $env:MYRENDERER_IBL='1'; $env:MYRENDERER_SHADOWS='1'
$env:MYRENDERER_BLOOM='1'; $env:MYRENDERER_GRID='0'; $env:MYRENDERER_AXES='0'
$env:MYRENDERER_GROUND='1'
```

| 基线图 | `MYRENDERER_RENDER_PATH` | `MYRENDERER_GBUFFER_DEBUG` |
| --- | ---: | ---: |
| `gp_p1_forward_final` | 0 | 0 |
| `gp_p1_deferred_final` | 1 | 0 |
| `gp_p1_gbuffer_albedo` | 1 | 1 |
| `gp_p1_gbuffer_normal` | 1 | 2 |
| `gp_p1_gbuffer_material` | 1 | 3 |
| `gp_p1_gbuffer_depth` | 1 | 4 |

手动重拍单张图时可以只设上面这一组环境变量再加一行路径开关，例如 Deferred 最终画面：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'; $env:MYRENDERER_MSAA='4'
$env:MYRENDERER_PBR='1'; $env:MYRENDERER_IBL='1'; $env:MYRENDERER_SHADOWS='1'
$env:MYRENDERER_BLOOM='1'; $env:MYRENDERER_GRID='0'; $env:MYRENDERER_AXES='0'
$env:MYRENDERER_GROUND='1'
$env:MYRENDERER_RENDER_PATH='1'; $env:MYRENDERER_GBUFFER_DEBUG='0'
$env:MYRENDERER_SCREENSHOT='build-release/p1a-deferred-final.png'
build-release/Release/Iris.exe assets/models/pbr_material_test.gltf
```

本文性能表的来源是 `deferred-benchmark`：同一夹具、四组路径与 MSAA 组合，预热 30 帧、测量 90 帧，JSON 写进 `build-release/deferred-benchmarks/`。环境变量覆盖项是 `MYRENDERER_BENCHMARK_WARMUP`、`MYRENDERER_BENCHMARK_FRAMES`、`MYRENDERER_BENCHMARK_OUTPUT`、`MYRENDERER_RENDER_WIDTH`、`MYRENDERER_RENDER_HEIGHT`、`MYRENDERER_MSAA`、`MYRENDERER_RENDER_PATH`、`MYRENDERER_GBUFFER_DEBUG`，以及上面那组 PBR / IBL / Shadows / Bloom / GRID / AXES / GROUND 开关。这些覆盖在场景加载后应用，因此只影响本次运行，不改写 `.myscene`。

## 下一步

1. 多光源扩展性的评估已由 GP-P1B 接手：`docs/local-light-stress.md` 记录 8/32/64 灯下 Forward 与 Deferred 的同档对照，64 灯时 Deferred 整帧 GPU P50 相对 Forward 约 `1.73×`，代价是额外 178 MiB RenderTarget 显存；对应 `todolist.md` 第 2.3 节 GP-P1 的多光源条目。
2. 本文「限制与取舍」里仍未开工的三项——Octahedral Normal Encoding、按像素/边缘着色或 TAA 替代 MSAA、Reversed-Z——在 `todolist.md` 中尚无独立条目；要推进需先把它们写成工作包，再评估它们是否会改动 `docs/images/` 的既有基线。
3. G-Buffer 的 Motion Attachment 由 GP-P1D 引入（TAA 与 SSAO），其屏幕空间效果与调试视图见 `docs/taa-ssao.md`；本文的 G-Buffer 布局表保持 GP-P1A 当时的口径，未随之改写。
