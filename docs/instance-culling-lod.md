# GP-P1C：Instancing、CPU Frustum Culling 与 LOD

- 记录日期：2026-08-29（本文与 `gp_p1c_baseline.png`、`gp_p1c_optimized.png` 两张基线图同属提交 `245b663`）
- 源码 revision：`245b663`；仓库当前 HEAD 为 `35a726c`。两张嵌入基线在 2026-09-16 的基线重锚定中被重拍，那一轮 56 张 PNG 的改动仍是未提交的工作树状态，依据见 [`regression-baseline-audit.md`](regression-baseline-audit.md)
- 构建目录：原文没有记录性能数字采集时使用的构建目录；本文「复现命令」保留原文写法 `build-release`（GCC/MinGW Release 树，见 [`../README.md`](../README.md)），MSVC 验收目录为 `build-ci-msvc`（Visual Studio 17 2022，Release，`BUILD_TESTING=ON`）
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0（原文环境行同时记录 1920×1080、4×MSAA、30 帧预热 + 90 帧测量、Forward PBR、关闭 VSync）

## 目标与范围

本阶段在同一个 `sphere.obj` GPU 模型上建立 50×50、共 2,500 个实例的固定压力场景。基线逐对象提交，优化路径按模型、Tint 和 LOD 分组后使用 `glDrawElementsInstanced`；CPU 先用世界空间包围球做视锥判断（frustum culling，视锥剔除），再按投影半径选择三档索引 LOD（level of detail，细节层级）。场景始终包含真实几何和固定镜头，不用空场 FPS 代表优化收益。

本阶段只批处理显式标记的压力场景不透明实例：透明排序、玻璃对象级缓存与阴影实例化仍走原路径，GPU-driven Culling 与层次包围结构也不在范围内，这些边界写在「限制与取舍」。

## 实现

### 数据流：从 RenderItem 到 `glDrawElementsInstanced`

```text
2,500 RenderItem transforms
          |
          v
local sphere -> world bounding sphere
          |
          +-- outside six frustum planes -> culled
          |
          v
projected radius in pixels -> LOD0 / LOD1 / LOD2
          |
          v
group by GpuModel + tint + LOD
          |
          v
stream mat4 instance buffer -> glDrawElementsInstanced
```

### 运行时：六个视锥平面与保守包围球

视锥由当前 View-Projection Matrix 提取六个归一化平面。包围球半径使用 Model Matrix 三个基向量中的最大缩放，非均匀缩放下仍保持保守，不会把实际可见物体误剔除。

### 运行时：按屏幕投影半径选择三档索引 LOD

LOD 使用屏幕投影半径而不是只看世界距离：1080p 固定基准中半径大于等于 14 px 使用 LOD0，7～14 px 使用 LOD1，小于 7 px 使用 LOD2。低档索引在 Mesh 上传时通过顶点网格聚类生成；三档共享同一 Vertex Buffer、材质与实例 Transform，只切换 Element Buffer。

### 渲染：批次键与实例缓冲

优化路径把可见实例按 `GpuModel` + Tint + LOD 组成批次，逐批次流式上传 `mat4` 实例缓冲并调用 `glDrawElementsInstanced`，因此每个批次只有一次 Draw Call，而几何仍是同一份真实网格。

### UI：压力预设与 `Instance submission stress`

`View → Instance / culling / LOD stress preset` 加载固定场景。Inspector 的 `Instance submission stress` 区域可分别切换 GPU Instancing、CPU Frustum Culling 与 Projected-size LOD，并显示 Submitted / Visible / Culled、三档实例数、提交三角形和 CPU 准备时间。四项统计与「1080p 实测」表的「可见 / 剔除」「LOD0 / 1 / 2」「提交三角形」以及 CPU 准备时间逐列对应，因此表里的每一格都能在界面上实时读出。

## 1080p 实测

环境：NVIDIA GeForce RTX 4060 Laptop GPU，OpenGL 3.3 / 驱动 591.44，1920×1080、4×MSAA、30 帧预热 + 90 帧测量、Forward PBR、关闭 VSync。下表测的是同一固定压力场景的四个提交阶段——逐对象基线、Instancing、加 Frustum Culling、再加 Projected-size LOD——在那一台参考机上的 Draw Call、可见/剔除实例数、三档 LOD 分布、提交三角形与 CPU / GPU 帧时间：

| 阶段 | Draw Calls | 可见 / 剔除 | LOD0 / 1 / 2 | 提交三角形 | CPU Frame P50 / P95 | GPU Frame P50 / P95 | Opaque P50 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 逐对象基线 | 2,501 | 2,500 / 0 | 2,500 / 0 / 0 | 800,000 | 5.386 / 6.484 ms | 1.465 / 2.827 ms | 1.329 ms |
| Instancing | 7 | 2,500 / 0 | 2,500 / 0 / 0 | 800,000 | 2.364 / 3.427 ms | 1.067 / 2.113 ms | 0.936 ms |
| + Frustum Culling | 7 | 2,390 / 110 | 2,390 / 0 / 0 | 764,800 | 2.290 / 3.430 ms | 1.061 / 2.072 ms | 0.929 ms |
| + Projected-size LOD | 19 | 2,390 / 110 | 170 / 2,041 / 179 | 477,636 | 2.077 / 3.128 ms | 0.653 / 0.941 ms | 0.521 ms |

完整路径相对基线把 Draw Call 减少 99.24%，提交三角形减少 40.30%，CPU Frame P50 降低 61.44%，GPU Frame P50 降低 55.43%。CPU 可见性、LOD 与批次准备耗时为 0.110 ms；LOD 增加到最多 18 个几何批次（6 种 Tint × 3 档），所以总 Draw Call 从纯 Instancing 的 7 上升到 19，但显著减少了顶点工作。

## 截图

两张图都由 `instance-stress-visual-regression` 目标采集：同一机位、同一 1920×1080、同一 4×MSAA、`MYRENDERER_RENDER_PATH=0`（Forward），夹具是 `assets/models/sphere.obj` 上的 2,500 实例压力场景。

### 基线 / 优化对照：提交路径变了，画面没变

左栏是逐对象提交的基线，右栏是 Instancing + Frustum Culling + Projected-size LOD 的完整路径。两栏构图、几何与着色一致，证明三个阶段的收益来自提交与几何路径，而不是删物体或改机位；相机外的 110 个包围球只在优化路径被剔除，因此不改变画面；远景 LOD 的几何差异集中在小于 14 px 的实例上，在这张固定机位图里读不出来。这两张模式各自拥有固定的 1920×1080、4×MSAA 视觉回归基线。

| 逐对象基线 | Instancing + Frustum Culling + LOD |
| --- | --- |
| ![逐对象提交基线：2,500 个实例各自一次 Draw Call，画面与优化路径同机位](images/gp_p1c_baseline.png) | ![完整优化路径的同一画面：构图与着色不变，证明收益来自提交与几何路径而非删物体](images/gp_p1c_optimized.png) |

## 验证

- `instance-stress-visual-regression` 是本文两张图的来源，也是画质验收口：它按固定环境变量重拍 2 张 1920×1080 图，写入 `build-release/instance-stress-visual-current/`，再与 `docs/images/` 的基线用 `MyRendererImageComparison` 以 MAE `0.015` / 变化像素 `0.08` 比对；采集或比对失败时报 `FATAL_ERROR`，因此该 target 失败即非零退出。加 `-DUPDATE_BASELINES=ON` 才会改写基线。
- `instance-stress-benchmark` 是「1080p 实测」表的来源：同一夹具、同一组开关，预热 30 帧、采样 90 帧，把 4 份 JSON 写进构建目录的 `instance-stress-benchmarks/`，文件名是 `gp_p1c_baseline`、`gp_p1c_instancing`、`gp_p1c_instancing_culling` 与 `gp_p1c_instancing_culling_lod`，与表的四行一一对应。
- 未验证项：表里的 CPU / GPU 帧时间、`0.110 ms` 的 CPU 可见性/LOD/批次准备耗时与 `110` 剔除计数都只在参考机上实测过一次，没有跨机器证据，随附 JSON 也没有入库，因此这些数字无法从仓库产物重新读出。
- 未验证项：`110` 与 LOD 分布（`170 / 2,041 / 179`）只对这一个固定机位、这一个 50×50 场景成立，换个镜头就会变；它们不是场景的固有属性。
- 未验证项：两张嵌入基线在 2026-09-16 的基线重锚定中被重拍（工作树状态，尚未提交）。`regression-baseline-audit.md` 记录的当时差异为「Instance stress | Fail | `gp_p1c_optimized`：MAE 0.00654899，变化 8.40205%」，本文没有逐像素复核重锚定前后的差异。

## 限制与取舍

- **当前只批处理显式标记的压力场景不透明实例**；透明排序、玻璃对象级缓存和阴影实例化仍走原路径。
- **LOD 是运行时顶点聚类生成的索引近似**，不是离线工具制作的美术 LOD；近景始终保留完整网格。
- **批次键目前包含精确 Tint**，因此六种颜色形成六个批次；后续可把颜色加入实例属性以进一步合批。
- **CPU Culling 为单线程线性扫描**；更大规模场景需要层次包围结构或 GPU-driven Culling（GPU 驱动的剔除）。本文没有这两者的实现或时间数据。
- **几何内存统计尚未把两份额外 LOD EBO 和每帧 Instance Buffer 峰值计入总显存估算**，性能表不虚报这部分数据。
- **「1080p 实测」表里的 0.110 ms 是 CPU 侧准备时间**，它只覆盖可见性判断、LOD 选择与批次装配，不等于整帧 CPU 时间（同一张表的 `5.386 / 2.364 / 2.290 / 2.077 ms` 才是 CPU Frame P50）。
- **两张基线图属于回归基线**，只能在明确批准的基线更新中改写；本轮重锚定尚未提交，因此本文嵌入的是工作树版本（见元信息块与「验证」）。

## 复现命令

可重复验收：

```powershell
cmake --build build-release --config Release --target instance-stress-visual-regression
cmake --build build-release --config Release --target instance-stress-benchmark
```

Benchmark 输出 Baseline、Instancing、Instancing+Culling、完整 LOD 四份 JSON；视觉目标重拍基线与完整优化两张图片。`instance-stress-visual-regression` 使用的固定环境变量是：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_MSAA='4'; $env:MYRENDERER_RENDER_PATH='0'
$env:MYRENDERER_INSTANCE_STRESS='1'
```

两次采集之间只有三个开关不同，每行对应一张基线图；场景参数固定为 `assets/models/sphere.obj`：

| 基线图 | `MYRENDERER_INSTANCE_OPTIMIZATION` | `MYRENDERER_FRUSTUM_CULLING` | `MYRENDERER_LOD` |
| --- | ---: | ---: | ---: |
| `gp_p1c_baseline` | 0 | 0 | 0 |
| `gp_p1c_optimized` | 1 | 1 | 1 |

手动重拍单张图时可以只设这一组环境变量，再运行 MSVC 树的 `build-ci-msvc/Release/Iris.exe`（`README.md` 记录的路径；`build-release` 是本仓库的 GCC Release 树）；例如完整优化路径：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_MSAA='4'; $env:MYRENDERER_RENDER_PATH='0'
$env:MYRENDERER_INSTANCE_STRESS='1'
$env:MYRENDERER_INSTANCE_OPTIMIZATION='1'
$env:MYRENDERER_FRUSTUM_CULLING='1'; $env:MYRENDERER_LOD='1'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_SCREENSHOT='build-ci-msvc/p1c-optimized.png'
build-ci-msvc/Release/Iris.exe assets/models/sphere.obj
```

可用的自动化变量是 `MYRENDERER_INSTANCE_STRESS=1`、`MYRENDERER_INSTANCE_OPTIMIZATION=0|1`、`MYRENDERER_FRUSTUM_CULLING=0|1` 与 `MYRENDERER_LOD=0|1`；它们和压力预设一起在启动时生效，因此可以和上面的手动重拍命令任意组合。Benchmark 的 JSON 写进 `build-release/instance-stress-benchmarks/`，不会覆盖 `docs/performance/` 下的任何版本化文件（该目录当前只有 Prism-5 的参考 JSON）。

## 下一步

1. 批次键把颜色从精确 Tint 改成实例属性，是本文「限制与取舍」第三条的直接修法：六种颜色目前必须形成六个批次，合批后 Draw Call 会低于 19。这一项尚未写进 `todolist.md`，需要先补成具体条目再动实现。
2. CPU Culling 换成层次包围结构（BVH 或空间网格）或 GPU-driven Culling，同样需要先在 `todolist.md` 立项；当前的线性扫描在 2,500 实例上只花 0.110 ms，因此这一项应当由更大的实例规模驱动，而不是提前实现。
3. 阴影与透明实例化进入同一条批次路径：本文只覆盖不透明压力场景实例，阴影实例化仍走原路径；这一项与 `todolist.md` P1-A 的室外阴影（CSM）工作有交集，需要一起评估。
