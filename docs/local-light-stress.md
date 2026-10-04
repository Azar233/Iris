# GP-P1B：多光源压力场景与 Forward / Deferred 曲线

- 记录日期：2026-08-26（本文与四张 `gp_p1b_*` 基线图同属提交 `c498c77`）
- 源码 revision：`c498c77`；仓库当前 HEAD 为 `35a726c`。四张嵌入基线在 `245b663`（GP-P1C 实例化提交）被改写，又随 2026-09-16 的基线重锚定再次重拍，那一轮 56 张 PNG 的改动仍是未提交的工作树状态，依据见 [`regression-baseline-audit.md`](regression-baseline-audit.md)
- 构建目录：原文没有记录性能数字采集时使用的构建目录；本文「复现命令」保留原文写法 `build-release`（GCC/MinGW Release 树，见 [`../README.md`](../README.md)），MSVC 验收目录为 `build-ci-msvc`（Visual Studio 17 2022，Release，`BUILD_TESTING=ON`）
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0（原文环境行同时记录 1920×1080、4× MSAA、30 帧预热 + 90 帧测量）

## 目标与范围

本阶段在 GP-P1A Hybrid Deferred 基线上加入 Point Light（点光）与 Spot Light（聚光），并建立 8 / 32 / 64 三档确定性压力场景。每档一半为点光、一半为聚光；Forward 与 Deferred 使用完全相同的光源数组、100 个物体、相机、材质、后处理和 4× MSAA，因此两条路径的差异只能来自着色路径本身。

本阶段有意不使用 Instancing，也不给局部灯加阴影：它验证的是多灯光下 Forward 与 Deferred 的着色扩展性，阴影与提交成本都要单独测量才不会混进这条曲线。这些边界与后续方向写在「限制与取舍」。

## 实现

### 运行时：三个 `vec4` Uniform Array 描述一个局部光源

每个局部光源压缩为三个 `vec4` Uniform Array（统一变量数组）：

| 数据 | 内容 |
| --- | --- |
| Position / Radius | XYZ 为世界坐标；W 的绝对值为作用半径，负号标记 Spot Light |
| Color / Intensity | 线性 RGB 与 HDR 强度 |
| Direction / Outer Cone | Spot 朝向与外锥余弦；Point Light 忽略该项 |

### 渲染：有限半径平滑截止、聚光锥与共用 BRDF

Point Light 使用带有限半径平滑截止的逆平方衰减：

```text
x = clamp(1 - (distance / radius)^4, 0, 1)
attenuation = x^2 / (1 + distance^2)
```

Spot Light 在相同距离衰减上叠加 `smoothstep(outerCos, innerCos, coneCos)`，因此锥体边缘连续，不产生硬切线。两条路径均使用相同 Cook-Torrance GGX BRDF；局部灯暂不投射阴影，以隔离光照循环本身的扩展成本。

### 压力场景：100 物体与 8 / 32 / 64 三档灯光

- 100 个独立 `RenderItem`，排列为 10×10 网格；每个对象保持独立 Draw Call，尚未使用 Instancing。
- 8 / 32 / 64 三档灯光均覆盖完整舞台，而不是只在一角增加灯光。
- 每档 Point / Spot 各半，使用固定彩色调色板、位置、半径、方向和强度。
- 固定黑色背景、低强度 IBL、哑光地面、固定 Hero Camera、1920×1080、4× MSAA。

不使用 Instancing 是有意的：这一阶段验证多灯光下 Forward 与 Deferred 的着色扩展性；下一项会在同一场景上实现 Instancing、CPU Frustum Culling 和 LOD，再单独测量提交与几何成本（见 [`instance-culling-lod.md`](instance-culling-lod.md)）。

### UI：`Local light stress` 预设与自动化入口

在 `View → Local light stress preset` 可一键加载固定立方体场景。也可以在 `Inspector → Renderer → Local light stress`：

1. 勾选 `Enable stress scene`。
2. 选择 `Low (8)`、`Medium (32)` 或 `High (64)`。
3. 在 `Opaque render path` 切换 Forward / Deferred (hybrid)。
4. 观察活动 Pass、GPU viewport、Draw calls 和 `Estimated opaque traffic`。

自动化入口为 `MYRENDERER_LIGHT_STRESS=1` 与 `MYRENDERER_LOCAL_LIGHT_TIER=0|1|2`；档位与界面上的 Low / Medium / High 一一对应，也就是 8 / 32 / 64 盏灯。

### 诊断：Attachment 流量是下限估算，不是硬件计数器

`Attachment traffic` 是根据 RenderTarget 格式、分辨率、MSAA 写入与 Resolve 计算的下限估算，不是 Nsight/驱动硬件 Counter。JSON 同时给出 `estimatedOpaqueTrafficBytesPerFrame`，以及用 GPU Frame P50 换算的 `estimatedOpaqueTrafficGiBPerSecondAtGpuP50`；后者用于同机趋势比较，不代表显卡真实总带宽利用率。

## 1080p 实测：8 / 32 / 64 灯扩展曲线

环境：NVIDIA GeForce RTX 4060 Laptop GPU，OpenGL 3.3 / 驱动 591.44，1920×1080，4× MSAA，30 帧预热 + 90 帧测量。下表测的是同一座 100 物体舞台在 8 / 32 / 64 三档灯光下、Forward 与 Deferred 两条路径的整帧 GPU 时间、不透明工作时间、Draw Call、估算 Attachment 流量与估算渲染显存，硬件就是环境行所述的那一台参考机：

| Lights | Path | GPU Frame P50 / P95 | Opaque work P50 | Draw Calls | Attachment traffic | Render memory |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 8 | Forward | 1.618 / 1.834 ms | 0.557 ms | 111 | 213.6 MiB/frame | 291.2 MiB |
| 8 | Deferred | 1.517 / 1.990 ms | 0.324 + 0.236 = 0.559 ms | 112 | 387.6 MiB/frame | 469.1 MiB |
| 32 | Forward | 2.314 / 2.847 ms | 1.364 ms | 111 | 213.6 MiB/frame | 291.2 MiB |
| 32 | Deferred | 1.779 / 2.209 ms | 0.312 + 0.509 = 0.821 ms | 112 | 387.6 MiB/frame | 469.1 MiB |
| 64 | Forward | 3.773 / 4.304 ms | 2.815 ms | 111 | 213.6 MiB/frame | 291.2 MiB |
| 64 | Deferred | 2.183 / 2.704 ms | 0.325 + 0.891 = 1.215 ms | 112 | 387.6 MiB/frame | 469.1 MiB |

Deferred 的 Geometry Pass 基本保持在 `0.31–0.33 ms`，灯光增长主要进入单一 Lighting Pass；Forward Opaque 从 `0.557 ms` 增至 `2.815 ms`。64 灯时 Deferred 整帧比 Forward 快约 **1.73×**，代价是额外 178 MiB RenderTarget 显存与约 174 MiB/frame 的估算 Attachment 流量。

由这张表得到的结论：

- 8 灯时 Forward 与 Deferred 的不透明工作时间几乎相同，Deferred 的额外全屏 Pass 尚未形成明显优势。
- 32 灯开始出现明确交叉；Deferred 把多物体重复灯光计算集中到一次屏幕空间 Lighting Pass。
- 64 灯时 Deferred 明显领先，但显存和 Attachment 带宽代价仍然存在。
- Draw Call 基本不随灯数变化；Deferred 固定多一次全屏 Lighting Draw。
- 结果证明的是当前 100 物体屏幕覆盖下的扩展性，不外推到所有场景。

## 截图

四张图都由 `local-lights-visual-regression` 目标采集：同一座 100 物体舞台、同一机位、同一 1920×1080、4× MSAA，夹具是 `assets/models/cube.obj`，只有渲染路径与灯数档位不同。它们是「同档两条路径画质一致」的证据，也是把上表的性能差异解释成着色路径差异、而不是画面差异的依据。

### 8 灯：Forward / Deferred 同档对照

低档灯光下舞台被 4 个 point + 4 个 spot 覆盖。两栏的亮度分布、彩色灯光落点与锥体边缘一致，证明 Deferred 的 Lighting Pass 复现了 Forward 的同一套光源数组与衰减；差异主要位于几何边缘的 MSAA G-Buffer Resolve 与高亮量化，不改变整体光照分布。

| 8 lights — Forward | 8 lights — Deferred |
| --- | --- |
| ![8 灯 Forward：4 个 point + 4 个 spot 的落点与锥体边缘](images/gp_p1b_forward_lights8.png) | ![8 灯 Deferred：同机位同舞台，亮度分布与 Forward 一致，证明两条路径共用同一光源数组](images/gp_p1b_deferred_lights8.png) |

### 64 灯：Forward / Deferred 同档对照

高档灯光下舞台被 32 个 point + 32 个 spot 覆盖，也就是本阶段的灯光上限。两栏仍然重合，说明上表里 64 灯 `1.73×` 的整帧差距来自 Deferred 把重复的逐物体灯光计算收进一次全屏 Lighting Pass，而不是某一条路径漏算或截断了灯光。

| 64 lights — Forward | 64 lights — Deferred |
| --- | --- |
| ![64 灯 Forward：32 个 point + 32 个 spot 覆盖整座舞台](images/gp_p1b_forward_lights64.png) | ![64 灯 Deferred：与 Forward 同档同机位重合，证明 1.73× 的差距来自着色路径而非灯光计算差异](images/gp_p1b_deferred_lights64.png) |

Forward / Deferred 同档比较结果：8 灯 MAE `0.000708`、变化像素 `0.861%`；64 灯 MAE `0.000787`、变化像素 `0.657%`。差异主要位于几何边缘的 MSAA G-Buffer Resolve 与高亮量化，不改变整体光照分布。32 档没有基线图——视觉回归只覆盖 8 与 64 两档，32 档只有 Benchmark JSON。

## 验证

- `local-lights-visual-regression` 是本文四张图的来源，也是画质验收口：它按固定环境变量重拍 4 张 1920×1080 图，写入 `build-release/local-lights-visual-current/`，再与 `docs/images/` 的基线用 `MyRendererImageComparison` 以 MAE `0.015` / 变化像素 `0.08` 比对；采集或比对失败时报 `FATAL_ERROR`，因此该 target 失败即非零退出。加 `-DUPDATE_BASELINES=ON` 才会改写基线。
- `local-lights-benchmark` 是「1080p 实测」表的来源：同一夹具、同一组开关，预热 30 帧、采样 90 帧，把 6 份 JSON 写进构建目录的 `local-lights-benchmarks/`，文件名是 `gp_p1b_forward_lights8`、`gp_p1b_deferred_lights8`、`gp_p1b_forward_lights32`、`gp_p1b_deferred_lights32`、`gp_p1b_forward_lights64` 与 `gp_p1b_deferred_lights64`，与表的六行一一对应。
- 未验证项：`0.000708 / 0.861%` 与 `0.000787 / 0.657%` 这两组同档 MAE 只出现在本文，仓库里没有随附的比较 JSON 或报告文件，因此本文无法从产物重新计算它们；数字按原文保留。
- 未验证项：表里的 GPU 时间、`178 MiB` 显存差与 `174 MiB/frame` 流量差都只在参考机上实测过一次，没有跨机器证据；`Attachment traffic` 本身是下限估算（见「诊断」），不是硬件 Counter。
- 未验证项：四张嵌入基线在 2026-09-16 的基线重锚定中被重拍（工作树状态，尚未提交）。`regression-baseline-audit.md` 记录的当时差异为「Local lights | Fail | `gp_p1b_forward_lights8`：MAE 0.0103143，变化 9.59833%」，本文没有逐像素复核重锚定前后的差异。

## 限制与取舍

- **OpenGL 3.3 Fragment Uniform 上限使本实现固定为最多 64 个局部灯**；更大规模应使用 UBO（uniform buffer object）/ SSBO（shader storage buffer object）。这也是压力场景最高只到 64 档的原因。
- **当前 Deferred Lighting 对屏幕中每个几何像素遍历全部灯光**，尚未实现 Light Volume、Tiled 或 Clustered Light Culling。因此上表里 Deferred 的优势随灯数增长，但成本仍与「屏幕像素 × 灯数」成正比。
- **局部灯没有 Cubemap Shadow 或 Spot Shadow**；加入阴影后必须单独报告 Shadow Pass 成本和显存，不能沿用本文这条只含着色成本的曲线。
- **估算带宽不含驱动内部压缩、Cache 命中、纹理过滤和 ROP 实际事务**，所以 `213.6 / 387.6 MiB/frame` 只能用于同机趋势比较。
- **不使用 Instancing 是本阶段的有意选择**：每个物体保持独立 Draw Call，因此表里 `111 / 112` 的 Draw Call 只反映 100 物体舞台与路径差异，不代表提交路径已经优化；GPU-driven 批量提交见 [`instance-culling-lod.md`](instance-culling-lod.md)。
- **四张 `gp_p1b_*` 基线图属于回归基线**，只能在明确批准的基线更新中改写；本次重锚定尚未提交，因此本文嵌入的是工作树版本（见元信息块与「验证」）。
  - **待复核**：这四张图在 `245b663`（GP-P1C 实例化提交）里被改写，而 `docs/regression-baseline-audit.md` 只把 `245b663` 记为 Glass-2C 基线的最后一次更新。本文未能核实这次改写是否走过批准的基线更新流程；图片路径与用途按原文保留不变。

## 复现命令

可重复验收：

```powershell
cmake --build build-release --config Release --target local-lights-visual-regression
cmake --build build-release --config Release --target local-lights-benchmark
```

`local-lights-visual-regression` 是本文四张图的来源，它使用的固定环境变量是：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_MSAA='4'; $env:MYRENDERER_GBUFFER_DEBUG='0'
$env:MYRENDERER_LIGHT_STRESS='1'
```

四次采集之间只有两个开关不同，每行对应一张基线图；场景参数固定为 `assets/models/cube.obj`：

| 基线图 | `MYRENDERER_RENDER_PATH` | `MYRENDERER_LOCAL_LIGHT_TIER` |
| --- | ---: | ---: |
| `gp_p1b_forward_lights8` | 0 | 0 |
| `gp_p1b_deferred_lights8` | 1 | 0 |
| `gp_p1b_forward_lights64` | 0 | 2 |
| `gp_p1b_deferred_lights64` | 1 | 2 |

手动重拍单张图时可以只设这一组环境变量，再运行 MSVC 树的 `build-ci-msvc/Release/Iris.exe`（`README.md` 记录的路径；`build-release` 是本仓库的 GCC Release 树）；例如 64 灯 Deferred 一侧：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_MSAA='4'; $env:MYRENDERER_GBUFFER_DEBUG='0'
$env:MYRENDERER_LIGHT_STRESS='1'; $env:MYRENDERER_LOCAL_LIGHT_TIER='2'
$env:MYRENDERER_RENDER_PATH='1'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_SCREENSHOT='build-ci-msvc/p1b-deferred-lights64.png'
build-ci-msvc/Release/Iris.exe assets/models/cube.obj
```

本文「1080p 实测」表的来源是 `local-lights-benchmark`：同一夹具与同一组开关（含 `MYRENDERER_RENDER_PATH` 与 `MYRENDERER_LOCAL_LIGHT_TIER`），预热 30 帧、采样 90 帧，把 6 份 JSON 写进 `build-release/local-lights-benchmarks/`；它按 Forward / Deferred × 8 / 32 / 64 逐档运行，任一档失败都会让该 target 失败。

## 下一步

1. 每条路径的灯光上限从 64 提到 UBO/SSBO 规模：这需要先把「每像素遍历全部灯光」换成 Light Volume、Tiled 或 Clustered Light Culling，否则灯数上去之后 Deferred 的 Lighting Pass 会重新变成瓶颈。这两项在 `todolist.md` 里都还没有条目，需要先立项再动实现。
2. 局部灯阴影：Cubemap Shadow 与 Spot Shadow 落地后，必须按本文「限制与取舍」的要求单独报告 Shadow Pass 成本与显存，不能与这张纯着色曲线混在一起。
3. 把 100 物体舞台接上 [`instance-culling-lod.md`](instance-culling-lod.md) 的批次路径，才能把提交成本从灯光成本里分离出来。GP-P1C 已经给出 `2,501 → 19` 的 Draw Call 对照，但那用的是 2,500 球压力场景：物体数、机位与屏幕覆盖都不同，两篇的数字不能直接互相换算。
