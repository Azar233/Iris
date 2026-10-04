# GP-P1E：glTF 骨骼动画与 GPU Skinning

- 记录日期：2026-09-01（本文与四张 `gp_p1e_*` 基线图同属提交 `7cf4059`）
- 源码 revision：`7cf4059`；仓库当前 HEAD 为 `35a726c`。四张嵌入基线里 `gp_p1e_bind_pose.png` 与 `gp_p1e_animated_pose.png` 在 2026-09-16 的基线重锚定中被重拍，`gp_p1e_joint_debug.png` 与 `gp_p1e_weight_debug.png` 内容未变；那一轮 56 张 PNG 的改动仍是未提交的工作树状态，依据见 [`regression-baseline-audit.md`](regression-baseline-audit.md)
- 构建目录：原文没有记录性能数字采集时使用的构建目录；本文「复现命令」沿用仓库既有写法 `build-release`（GCC/MinGW Release 树，见 [`../README.md`](../README.md)），MSVC 验收目录为 `build-ci-msvc`（Visual Studio 17 2022，Release，`BUILD_TESTING=ON`）
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / OpenGL 3.3。原文「1080p 实测」的环境行只记了 GPU 与 OpenGL 版本，驱动 `591.44` 取自 [`regression-baseline-audit.md`](regression-baseline-audit.md) 记录的同一台验收参考机

## 目标与范围

本阶段完成 glTF 骨骼动画的最小闭环：资产导入保存 `JOINTS_0`、`WEIGHTS_0`、Skin Joint（蒙皮关节）、Inverse Bind Matrix（逆绑定矩阵）、节点层级和 Animation Clip（动画片段）；CPU 按时间采样 Translation / Rotation / Scale，GPU Vertex Shader（顶点着色器）使用关节矩阵调色板完成 GPU skinning（GPU 蒙皮），也就是 Linear Blend Skinning（线性混合蒙皮）。本文记录这条路径的资产格式、运行时采样、共用蒙皮公式、调试视图与 1080p 实测。

本阶段明确不做的是角色动画系统：当前只播放一个 Clip，不支持 Animation Blending（动画混合）、Cross-fade（交叉淡入淡出）、Additive Layer（叠加层）或 Root Motion（根运动），运行时也不保留 glTF 的 `STEP` / `CUBICSPLINE` 插值语义。这些边界逐条写在「限制与取舍」。

## 实现

### 资产导入：每顶点四权重、每 Mesh 独立 Skin Palette

每个顶点最多保留四个权重最高的关节影响，并在导入后归一化。每个 Mesh 保存独立 Skin Palette，因此多个蒙皮 Mesh 可以共享节点骨架，同时保持各自 Mesh Bind Transform（网格绑定变换）。当前 OpenGL 3.3 路径每个 Mesh 最多上传 64 个 `mat4` 关节矩阵。

### 运行时：从 Bind TRS 到 Global Joint Transform

运行时从 Bind TRS（绑定平移/旋转/缩放）开始，将当前 Animation Channel（动画通道）的 Translation/Scale 做线性插值、Rotation 做 Quaternion Slerp（四元数球面插值），再按父子层级生成 Global Joint Transform（全局关节变换）。最终矩阵为：

```text
Joint Matrix = Animated Node Global × Mesh-adjusted Inverse Bind Matrix
Skinned Vertex = Σ(weight[i] × Joint Matrix[i] × Bind Vertex)
```

动画关闭时仍执行 Skin Palette，但节点使用 Bind Pose（绑定姿势）；这同时验证 Inverse Bind Matrix 是否正确——模型应保持笔直，不能出现位置跳变或缩放。

### 渲染：六条 vertex pass 共用同一蒙皮公式

主 Forward/G-Buffer、方向光 Shadow、Transmission Shadow（透射阴影）、Light-space Caustics（灯光空间焦散）与 Glass Thickness Vertex Pass（玻璃厚度顶点 pass）共用同一蒙皮公式，避免主体动画和阴影/辅助缓冲不同步。

### UI：Clip 播放、时间 Scrub 与两种 Skinning Debug

Inspector / Renderer 提供 Clip 选择、播放/暂停、时间 Scrub、速度，以及两种 Skinning Debug（蒙皮调试视图）：

- Joint Influence（关节影响）：以确定性颜色混合显示各顶点的关节影响。
- Dominant Weight（主权重）：显示当前最大权重，方便发现未归一化、硬断层或权重过散。

### 验证夹具：`assets/models/skinning_test.gltf`

`assets/models/skinning_test.gltf` 是项目内生成的 3-Joint、10-Vertex、8-Triangle 固定资源。Wave Clip 长 3 秒，在 1 秒处将中间关节绕 Z 轴旋转 35°。它只用于验证正确性，不是性能夹具，这一点写在「1080p 实测」与「限制与取舍」里。

## 1080p 实测

环境：NVIDIA GeForce RTX 4060 Laptop、OpenGL 3.3、Deferred、4×MSAA、预热 30 帧并采样 90 帧。下表测的是同一固定夹具在绑定姿势、动画姿势与关节调试视图三种状态下的 CPU / GPU 帧时间与 Draw Call 计数，硬件就是环境行所述的那一台参考机：

| 配置 | CPU Frame P50/P95 | GPU Frame P50/P95 | Draw Call |
|---|---:|---:|---:|
| Bind Pose | 1.467 / 2.464 ms | 0.592 / 0.764 ms | 3 |
| Animated Pose | 1.707 / 2.660 ms | 0.675 / 1.453 ms | 3 |
| Joint Debug | 1.550 / 2.554 ms | 0.650 / 1.223 ms | 3 |

该固定资产用于验证正确性，不用于宣称大规模角色性能；它只有 10 个顶点，GPU 数字容易受驱动与查询抖动影响。真正的扩展性测试需要高顶点角色、多个实例和多关节档位。

## 截图

四张图都由 `skinning-visual-regression` 目标采集，固定 1920×1080、4×MSAA、`MYRENDERER_RENDER_PATH=1`（Deferred）、`MYRENDERER_TAA=0`、`MYRENDERER_BLOOM=0`，并且不传场景参数，因此应用按 `MYRENDERER_ANIMATION_DEMO=1` 加载 `assets/models/skinning_test.gltf`。

### 绑定姿势与动画姿势：蒙皮公式的端点证据

左栏是动画关闭时的绑定姿势，右栏是 Wave Clip 在 1.0 秒处的采样。绑定姿势下模型保持笔直，说明仍在执行的 Skin Palette 与 Bind Pose 互相抵消，也就是 Inverse Bind Matrix 正确、没有位置跳变或缩放；动画姿势下中间关节绕 Z 轴旋转 35°、网格随关节弯曲，说明关节矩阵调色板确实在 Vertex Shader 里按四权重驱动了顶点，而不只是采样出了 CPU 侧的姿态数据。

| 绑定姿势（动画关闭） | Wave Clip 1.0 秒（中间关节绕 Z 轴 35°） |
| --- | --- |
| ![动画关闭时的绑定姿势：模型保持笔直，证明 Inverse Bind Matrix 与 Bind Pose 互相抵消](images/gp_p1e_bind_pose.png) | ![Wave Clip 1.0 秒姿势：关节矩阵调色板按四权重驱动顶点，网格在中间关节处弯曲](images/gp_p1e_animated_pose.png) |

### 两种 Skinning Debug：关节影响与主权重

左栏的 Joint Influence 视图把插值后的关节调色板影响以确定性颜色混合画在顶点上，因此可以读出权重是否按预期分布；右栏的 Dominant Weight 视图显示每个顶点的当前最大权重，用于检查归一化、硬断层与权重过散。两张图都在 `uSkinningDebugView` 非 0 时直接覆盖 G-Buffer 的 base color（材质贴图被调试颜色取代，不再参与画面），因此读到的颜色只反映关节索引与权重；它们证明导入后的四权重确实落在顶点属性上，而不是只在导入测试里成立。

| Joint Influence 视图 | Dominant Weight 视图 |
| --- | --- |
| ![Joint Influence：以确定性颜色混合显示各顶点的关节影响](images/gp_p1e_joint_debug.png) | ![Dominant Weight：显示各顶点当前最大权重，用于检查归一化与硬断层](images/gp_p1e_weight_debug.png) |

四张图的重拍命令与逐图开关见「复现命令」；`skinning-visual-regression` 每次运行都会把新采集写入构建目录再与 `docs/images/` 里的这四张基线逐像素比对，常规运行不会改写基线。

## 验证

- `asset-import`（CTest，`ctest --test-dir build-release -R asset-import --output-on-failure`）对 `skinning_test.gltf` 断言四件事：三关节 Skin Palette 与一个 Animation Clip 被完整导入；Animation Duration 转换为秒；四个 Rotation Keyframe 保留；每个顶点的四权重之和为 1。这四条把夹具的导入结果逐项固定，而不是只验证「模型能显示出来」。
- `skinning-visual-regression` 是画质验收口：固定比较 Bind Pose、1.0 秒动画姿势、Joint Influence 和 Dominant Weight 共四张 1920×1080、4×MSAA 图片，每张图与 `docs/images/` 的基线用 `MyRendererImageComparison` 比对，阈值 MAE `0.01` / 变化像素 `0.04`；采集失败或比对越界都会报 `FATAL_ERROR`，也就是该 target 非零退出。加 `-DUPDATE_BASELINES=ON` 才会写回基线，常规运行不碰它。
- `skinning-benchmark` 产出本文「1080p 实测」表：同一夹具与同一组开关，预热 30 帧、采样 90 帧，把 3 份 JSON 写进构建目录的 `skinning-benchmarks/`，文件名是 `gp_p1e_bind_pose`、`gp_p1e_animated_pose` 与 `gp_p1e_joint_debug`；文件名与表的三行一一对应（表里没有 Dominant Weight 行，benchmark 也不采集它）。
- 未验证项：本文的 CPU / GPU 帧时间只在 [`regression-baseline-audit.md`](regression-baseline-audit.md) 记录的那一台参考机上实测，没有跨机器或跨驱动证据；该参考机上的驱动版本在本文原文里没有记录（元信息块给出的 `591.44` 来自同一份审计记录）。性能表随附的 3 份 JSON 没有入库，`docs/performance/` 下也没有对应的版本化文件，因此表里每一格都无法从仓库产物重新读出。
- 未验证项：四张嵌入基线里有两张（`gp_p1e_bind_pose.png`、`gp_p1e_animated_pose.png`）是 2026-09-16 基线重锚定后的工作树版本，另外两张未变；`regression-baseline-audit.md` 对 Skinning 套件的记录是「关节/权重调试视图回到 MAE 0」，本文没有逐张核对这四张图在重锚定前后的像素差异。

## 限制与取舍

- **当前只播放一个 Clip**，不支持 Animation Blending、Cross-fade、Additive Layer 或 Root Motion。这是最小闭环的范围选择，不是实现遗漏。
- **运行时统一采用 Linear/Slerp**，不保留 glTF `STEP` / `CUBICSPLINE` 插值语义。本文的固定夹具只用 LINEAR 采样，因此这条限制不影响本阶段的画质结论，但它意味着外部资产的插值语义会被简化。
- **每顶点最多四权重、每 Mesh 最多 64 关节**；超过 64 会给出导入警告并截断。上限来自 OpenGL 3.3 路径的 `mat4` 关节矩阵数组，而不是模型层的限制。
- **Bounds 仍来自 Bind Pose**，剧烈动画可能超出视锥剔除包围球；蒙皮模型暂不进入实例 LOD 压力路径。
  - **待复核**：工作树里 `GpuModel::updateAnimation` 已经按关节位移重算 `dynamicBoundsRadius_`，`GpuModel::boundsRadius()` 也把它返回给 `Renderer` 的包围球，`todolist.md` P2-C 把「Skinned Mesh 动态 Bounds」记为兼容维护项。因此这条限制只对本文记录的 revision `7cf4059` 成立；本文没有核实工作树里那份动态 bounds 是否已经接到视锥剔除路径，所以按原文保留结论。
- **GP-P1D 的 TAA Motion Vector 仍只覆盖相机运动**；骨骼顶点的上一帧位置尚未写入 Motion Buffer，因此动画与 TAA 同时开启时可能出现 Ghosting。
  - **待复核**：工作树里 `gbuffer.vert` 已经按 `uPreviousJointMatrices` 计算上一帧蒙皮位置，`gbuffer.frag` 据此写 `gMotion`，`GpuModel` 也在上传 `uPreviousSkinningValid`；`todolist.md` 2.4 把「相机、对象和骨骼上一帧数据、统一 History Reset、Jitter 与动态 Motion Vector」记为 SR-P0 已完成。因此这句话同样只对 revision `7cf4059` 成立，本文未能核实的是它指的是光栅 Motion Buffer 的哪一条路径。
- **10 顶点夹具上的 GPU 数字不代表角色性能。** 表里的 `0.592 / 0.675 / 0.650 ms` 是这一颗固定资产在参考机上的读数，容易受驱动与查询抖动影响；要谈扩展性必须先有高顶点角色、多个实例与多关节档位。
- **四张基线图属于回归基线**，只能在明确批准的基线更新中改写；本轮重锚定尚未提交，因此本文嵌入的是工作树版本（见元信息块与「验证」）。

## 复现命令

视觉回归与 Benchmark：

```powershell
cmake --build build-release --target skinning-visual-regression
cmake --build build-release --target skinning-benchmark
```

`skinning-visual-regression` 是四张图的来源：它按固定环境变量重拍 4 张 1920×1080 图，写入 `build-release/skinning-visual-current/`，再与 `docs/images/` 的基线逐像素比对（MAE `0.01` / 变化像素 `0.04`）；加 `-DUPDATE_BASELINES=ON` 才会改写基线。它使用的固定环境变量是：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_ANIMATION_DEMO='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_MSAA='4'; $env:MYRENDERER_RENDER_PATH='1'
$env:MYRENDERER_TAA='0'; $env:MYRENDERER_BLOOM='0'
```

四次采集之间只有三个开关不同，每行对应一张基线图；不传场景参数，演示模式加载的就是 `assets/models/skinning_test.gltf`：

| 基线图 | `MYRENDERER_ANIMATION` | `MYRENDERER_ANIMATION_TIME` | `MYRENDERER_SKIN_DEBUG` |
| --- | ---: | ---: | ---: |
| `gp_p1e_bind_pose` | 0 | 0.0 | 0 |
| `gp_p1e_animated_pose` | 1 | 1.0 | 0 |
| `gp_p1e_joint_debug` | 1 | 1.0 | 1 |
| `gp_p1e_weight_debug` | 1 | 1.0 | 2 |

手动重拍单张图时可以只设这一组环境变量，再运行 `build-ci-msvc/Release/Iris.exe`（不传模型路径；该可执行文件路径与 [`../README.md`](../README.md) 一致，`build-release` 是本仓库的 GCC Release 树）；例如 Dominant Weight 诊断图：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_ANIMATION_DEMO='1'
$env:MYRENDERER_ANIMATION='1'; $env:MYRENDERER_ANIMATION_TIME='1.0'; $env:MYRENDERER_SKIN_DEBUG='2'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_MSAA='4'; $env:MYRENDERER_RENDER_PATH='1'
$env:MYRENDERER_TAA='0'; $env:MYRENDERER_BLOOM='0'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_SCREENSHOT='build-ci-msvc/p1e-weight-debug.png'
build-ci-msvc/Release/Iris.exe
```

本文「1080p 实测」表的来源是 `skinning-benchmark`：同一夹具与同一组开关（含 `MYRENDERER_ANIMATION`、`MYRENDERER_ANIMATION_TIME`、`MYRENDERER_SKIN_DEBUG`），预热 30 帧、采样 90 帧，把 JSON 写进 `build-release/skinning-benchmarks/`。

CPU 侧断言可以直接用 CTest 复核：

```powershell
ctest --test-dir build-release -R asset-import --output-on-failure
```

## 下一步

1. 把 GPU 蒙皮演示面板收口到 `EditorCommand`：`todolist.md` P1-0A 的 A2b2b 条目把「GPU 蒙皮与 Prism 演示面板」列为仍直接写入界面的剩余面，收口后动画开关与调试视图才会和场景/Preset 一样走统一入口。
2. Skinned Mesh 的动态 Bounds 与上一帧 Skin 数据按 `todolist.md` P2-C 只做兼容维护；本文「限制与取舍」的两条「待复核」要先核实工作树状态，再决定是否需要改写结论。
3. Animation Blending / State Machine / IK / Root Motion / Morph / FBX Animation 在 `todolist.md` 第 6 节「明确后移或舍弃」里是「后移且默认不做」，重新进入条件是出现角色 Hero Scene；本文不做预研。
