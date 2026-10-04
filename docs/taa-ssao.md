# GP-P1D：TAA 与 SSAO

- 记录日期：2026-09-20
- 源码 revision：`35a726c`；本阶段的实现目前只在工作树里（`shaders/temporal_aa.frag`、`shaders/ssao.frag`、`shaders/ssao_blur.frag`、`src/render/SsaoRenderer.h` / `.cpp`、`src/render/PostProcessor.h` / `.cpp` 的 TAA 历史资源，以及 `src/app/Application.cpp` 里的环境变量覆盖都尚未入库）
- 构建目录：性能数字来自参考构建目录 `build-ci-msvc`，Visual Studio 17 2022，Release，`BUILD_TESTING=ON`；本文「复现命令」按仓库既有写法使用 `build-release`
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0
- 测量环境：1920×1080、Deferred、1× MSAA、Bloom Off，预热 30 帧并采样 90 帧
- 基线决策与验收面背景见 [`regression-baseline-audit.md`](regression-baseline-audit.md)

## 目标与范围

本阶段在既有 Forward / Hybrid Deferred 管线上增加两项屏幕空间效果：TAA（temporal anti-aliasing，时序抗锯齿）与 SSAO（screen-space ambient occlusion，屏幕空间环境光遮蔽）。两项都保持默认关闭，因此没有启用它们的场景与既有 Forward / Deferred 基线逐像素一致，`docs/images/` 与 `docs/reference-images/` 中的固定图没有被本阶段改写。

范围上做了三条明确约束：

- **SSAO 只服务 Hybrid Deferred。** 它读取 G-Buffer 的 Depth 与 Encoded World Normal，Forward 路径没有常驻 G-Buffer，因此 Inspector 里 SSAO 的开关与滑杆在非 Deferred 路径下整体禁用。
- **TAA 位于 HDR 场景完成之后、Bloom 与 Tone Mapping 之前。** 它解析的是 HDR 场景颜色与深度，Bloom 从解析后的结果提取，因此时序收敛发生在显示变换之前。
- **TAA 的时序诊断视图只服务 Deferred 路径**，因为它们依赖 G-Buffer 的 Motion Attachment；Forward 路径的 TAA 只有相机重投影。

两项效果都不改变光照模型：SSAO 只乘到 Ambient/IBL 上，TAA 只改变抗锯齿与时间稳定性。

## 实现

### 运行时：TAA 的 Jitter 与历史资源

每帧用 8 样本 Halton(2,3) 序列偏移投影矩阵：帧序号对 8 取模再加 1 得到样本下标，`halton(sample, 2)` 与 `halton(sample, 3)` 各减 `0.5` 后按分辨率折算成投影矩阵的像素偏移。这是逐帧交替的亚像素抖动，而不是每像素随机，因此同一帧内所有像素共享同一个偏移，运动矢量与历史重投影才对得上。

历史资源是 ping-pong 的两组，每组三张全分辨率纹理：RGBA16F Color、R32F Depth 与 RG16F Motion。`PostProcessor` 在首次 resize 时一次性分配，之后靠切换读写索引推进，而不是拷贝。

### 运行时：历史重投影（history reprojection）、邻域钳制（neighborhood clamp）与拒绝条件

Temporal Resolve 读取当前帧深度，用逆 View-Projection（`uInverseCurrentViewProjection`）重建世界坐标，再投影到上一帧的 View-Projection（`uPreviousViewProjection`）得到 Motion Vector 与 History UV。Motion Vector 同时写进运动附件，供下一帧与诊断视图使用。

历史颜色的接受条件分三层：

1. **历史有效性与屏幕内判定。** 历史尚未建立、TAA 刚被打开，或重投影落在 `[0,1]` 之外时，直接输出当前帧颜色，历史权重记为 0。
2. **深度兼容性判定。** 上一帧深度与本次重投影推出的期望深度之差超过 `max(0.0015, 0.015 * (1 - currentDepth))` 时视为不兼容，同样退回当前帧。阈值随深度收窄，避免远平面附近的正常深度量化被误判成 disocclusion（去遮挡）。
3. **邻域钳制（neighborhood clamp）。** 历史颜色被钳制到当前帧 3×3 邻域的 RGB Min/Max AABB 内，越界部分被拉回。

通过三层判定的像素按 `mix(current, history, uHistoryWeight)` 混合，默认历史权重 0.90；历史输出同时写入该权重值，这就是 History Weight 诊断视图的来源。

分辨率变化、TAA 开关变化或历史首次建立都会自动重置：`PostProcessor::resize` 与 `resetTemporalHistory` 都会把 `historyValid_` 置为 false，另有一条投影矩阵跳变超过 `0.35` 的重置条件。

### UI：Inspector 的 `Post processing` 分组

Inspector 的 `Post processing` 分组提供 `Temporal AA` 开关、`TAA history weight`（0.00～0.98）与 `TAA debug` 三种视图：`Final`、`Motion vectors` 与 `History weight`。诊断视图依赖 G-Buffer 的 Motion Attachment，因此只在 Deferred 路径下有意义。

### 运行时：SSAO 的采样与滤波

SSAO 从 G-Buffer Depth 重建 View-space Position，并把 Encoded World Normal 变换到观察空间。16 个确定性半球样本在构造时生成（以径向反演铺开的半球分布，带 `0.1 + 0.9 * u²` 的尺度收敛），每个像素再用哈希函数得到一个随机旋转角，把样本核转到该像素的切空间。逐样本做深度测试：落在屏幕外或天空的样本被跳过，命中时按 `rangeWeight`（视线深度差与半径的 `smoothstep`）加权计数，最后用 `pow(clamp(1 - occluded / 16, 0, 1), uStrength)` 给出遮蔽值。**这套样本核是确定性的，逐像素的随机性只来自旋转角**，因此同一帧重复采集得到同一结果。

原始遮蔽图再用 5×5 深度感知滤波（depth-aware blur）平滑：空间权重为 `exp(-(x² + y²) * 0.22)`、深度权重为 `exp(-|sampleDepth - centerDepth| * 900)`，两者相乘后做归一化加权平均。深度项让滤波不跨越物体边界，避免把前景的遮蔽涂到背景上。

### 渲染：SSAO 只乘到 Ambient/IBL

SSAO 结果只乘到 Ambient/IBL 项上，不削弱方向光和局部直接光。这是本阶段有意选择的合成策略：遮蔽表达的是环境光的可见性，不是直接光的衰减，把它乘到直接光上会在接触处产生不属于该光照模型的暗边。`Radius`、`Bias` 与 `Strength` 可在 Inspector 里实时调整；`G-buffer debug` 的 `SSAO` 视图单独显示遮蔽结果，用于在不看最终画面的前提下检查遮蔽本身。

### 诊断：GPU Pass 时间与显存估算

`Renderer` 逐 pass 记录 GPU 时间（`GL_TIME_ELAPSED` 查询环），因此 Benchmark JSON 的 `gpuPasses` 会按 pass 名分列 P50/P95。TAA 解析与 Bloom、Tone Mapping 合并在同一条最终合成 pass 里，所以实测表出现的是 `TAA + tone map` 这一合并条目，而不是单独的 TAA 项。显存侧，`Renderer::estimatedRenderMemoryBytes()` 把 RenderTarget、G-Buffer、PostProcessor、ShadowMap、EnvironmentMap、SSAO 与 Caustics 的估算相加，表中的「估算渲染资源」即该值。

## 1080p 实测

环境：NVIDIA GeForce RTX 4060 Laptop、OpenGL 3.3、1920×1080、Deferred、1× MSAA、Bloom Off，预热 30 帧并采样 90 帧。

| 配置 | GPU Frame P50 | GPU P95 | 相关 Pass P50 | Draw Call | 估算渲染资源 |
|---|---:|---:|---:|---:|---:|
| Baseline | 0.536 ms | 0.541 ms | Tone map 0.038 ms | 24 | 306.9 MiB |
| SSAO | 1.258 ms | 2.154 ms | SSAO 0.727 ms | 26 | 314.8 MiB |
| TAA static | 0.694 ms | 1.011 ms | TAA + tone 0.228 ms | 25 | 306.9 MiB |
| TAA moving | 0.695 ms | 0.723 ms | TAA + tone 0.227 ms | 25 | 306.9 MiB |
| SSAO + TAA moving | 1.457 ms | 2.357 ms | SSAO 0.728 ms；TAA + tone 0.230 ms | 27 | 314.8 MiB |

由此得到的结论：SSAO 是本阶段的主要成本，单独启用时 GPU Frame P50 从 `0.536 ms` 升到 `1.258 ms`，其中 `0.727 ms` 落在 SSAO pass 上；TAA 的解析开销在 `0.23 ms` 量级，静止与运动相机几乎相同（`0.228 ms` 对 `0.227 ms`）。TAA 静止一侧的 P95（`1.011 ms`）高于运动一侧（`0.723 ms`），本文没有解释这一项的来源，按原数字保留，理由见「限制与取舍」。

PostProcessor 当前会在首次 resize 时预分配 TAA ping-pong 资源，因此表中 Baseline 已包含约 63.3 MiB TAA 历史资源；SSAO 启用后额外分配两张 R16F 全分辨率纹理，约 7.9 MiB。该策略避免运行时开关带来的分配抖动，但不是最低显存方案。

## 截图

五张图都由 `screen-space-visual-regression` 目标在同一机位、同一 1920×1080、同一 1× MSAA Deferred 配置下采集，夹具是 `assets/models/pbr_material_test.gltf`。

### TAA 在运动相机下的时序解析

运动场景每帧以固定角速度旋转相机（`camera_.orbit(0.012f, 0.0f)`），因此输出可重复。该图证明 TAA 在相机持续运动、几何边缘逐帧移动的情况下仍解析出稳定的边缘，而没有把上一帧内容拖成重影；同目录的 `gp_p1d_taa_static.png` 是同一配置下的静止相机对照。

![TAA 运动相机时序解析：走动中的几何边缘保持稳定，无可见重影](images/gp_p1d_taa_motion.png)

### 运动矢量诊断：重投影是否处处成立

该图把 Temporal Resolve 写出的 Motion Vector 直接显示出来，用于检查重投影：向量长度对应像素位移，背景与前景的边界处应当是可解释的连续场，而不能出现大块方向错误的区域。它证明相机运动确实被转换成每像素的重投影位移，这正是历史采样位置正确的依据。

![重投影运动矢量诊断：向量长度对应像素位移，用于定位 disocclusion 与错误重投影](images/gp_p1d_motion_vectors.png)

### 历史权重诊断：历史被接受了多少

该图显示每个像素最终接受的历史权重。深度不兼容、重投影出屏或历史尚未建立的位置权重为 0（历史被完全拒绝），运动矢量与深度都一致的位置权重为 `0.90`。它证明邻域钳制与两层拒绝条件不是只写在代码里，而是在边缘与遮挡变化处真的把历史拒掉了，也就是 ghosting 控制的证据。

![历史权重诊断：边缘与遮挡变化处历史被拒绝，静止表面接受 0.90 历史](images/gp_p1d_history_weight.png)

### SSAO 的最终合成：只压暗环境光

该图是启用 SSAO 后的最终画面。对比同目录的 `gp_p1d_baseline.png`（未启用 SSAO 的基准），接触处、凹陷处与遮蔽角落变暗，而方向光与局部直接光的受光面亮度不变，证明遮蔽只乘到 Ambient/IBL 上，没有削弱直接光。

![SSAO 最终合成：接触处与遮蔽角落变暗，直接光受光面不受影响](images/gp_p1d_ssao_final.png)

### SSAO 的原始遮蔽视图

该图是 `G-buffer debug` 的 `SSAO` 视图，显示经过 5×5 深度感知滤波后的遮蔽值本身，绕开天空、Overlay、透明与后处理。它证明遮蔽场在几何边界处被深度项切断、在平面内部连续，而不是把噪声直接交给最终画面。

![原始深度感知遮蔽视图：几何边界处遮蔽被切断，平面内部保持连续](images/gp_p1d_ssao_debug.png)

## 验证

- `editor-session`（CTest）覆盖后处理载荷的往返：`ssaoEnabled`、`ssaoStrength`、`temporalAaEnabled` 与 `temporalHistoryWeight` 提交后按 `SetPostProcessingSettings` 原值读回，因此两项效果的 Inspector 参数不是只改了界面状态。
- `scene-document-repeat-load` 覆盖 `.myscene` 持久化：带有 `ssaoEnabled` 的渲染器设置在保存—重载后仍为 `true`，并且仍走 Deferred 路径。
- `gpu-smoke` 用真实 OpenGL 上下文运行应用，因此 TAA 与 SSAO 的着色器必须真正编译链接，而不只是在单元测试里成立。
- `screen-space-visual-regression` 是官方验收口：在 1920×1080、1× MSAA、Deferred、`pbr_material_test.gltf` 上固定比较 Baseline、SSAO Final/Debug、TAA Static/Moving、Motion Vector 与 History Weight 共 7 张图，对每张图用 `MyRendererImageComparison` 以 MAE `0.02`、变化像素 `0.10` 为阈值比对；采集或比对失败时报出 `FATAL_ERROR`，因此该 target 失败即非零退出。
- `regression-baseline-audit.md` 记录本套件在 Kloofendal EXR 成为新固定输入后随基线更新通过：在参考机器上独立重跑对每张图报出 MAE `0` 与变化像素 `0%`，阈值没有被放宽。
- 未验证项：表中 Draw Call 的增量（`24 → 26 → 27`）与 `306.9 / 314.8 MiB` 的显存估算公式本文没有逐项核对，见「限制与取舍」的对应条目。

## 限制与取舍

- **Motion Vector 由相机矩阵与当前深度生成，能覆盖相机运动，但尚未保存每个 RenderItem 的上一帧 Model Matrix，因此独立运动物体会被当成静态物体。**
  - **待复核**：当前工作树里 `RenderItem::previousModelMatrix` 与 `SceneEntity::previousWorldTransform` 已经存在，`gbuffer.frag` 也按 `uPreviousModel` 计算 `gMotion`，因此「尚未保存上一帧 Model Matrix」这一描述与工作树代码不完全一致；同时实例化批次显式把 `uMotionHistoryValid` 置为 false，即实例化物体不提供逐物体运动矢量。原句按原样保留，未能核实的是两者是否指同一件事。本阶段未追踪那两处字段的赋值时机，因此没有替原作者改写结论。
- **SSAO 是屏幕空间近似**：屏幕外遮挡、极薄几何和大半径会产生信息缺失；当前 16 样本 + 全分辨率 5×5 滤波更偏画质验证，后续可改为半分辨率、蓝噪声与时序积累。
- **History Clamp 使用 RGB AABB，不是 YCoCg/Variance Clip**；高对比细线更稳健的方案仍可继续演进。
- **TAA 与 MSAA 可以同时开启**，但本阶段性能/画质对照固定在 1× MSAA，明确展示 TAA 自身成本。
- **表中的「估算渲染资源」是代码侧估算，不是 GPU 实测分配**：它由 `Renderer::estimatedRenderMemoryBytes()` 把各子系统的 `estimatedBytes()` 相加得到，不含驱动簿记与分配器填充；本文没有跨机器或跨驱动的显存实测。
- **性能数字只有一台参考机的证据**：`0.536 / 1.258 / 0.694 / 0.695 / 1.457 ms`、`306.9 / 314.8 MiB` 与 Draw Call 计数都来自同一台 NVIDIA GeForce RTX 4060 Laptop GPU（驱动 591.44，OpenGL 3.3.0）。
- **TAA 的 P95 在静止相机下高于运动相机**（`1.011 ms` 对 `0.723 ms`）。这一项在本文中没有解释，也没有重测确认，保留原数字。

## 复现命令

```powershell
cmake --build build-release --target screen-space-visual-regression
cmake --build build-release --target screen-space-benchmark
```

`screen-space-visual-regression` 就是本文五张图的来源：它按固定环境变量重拍 7 张 1920×1080 图，写入 `build-release/screen-space-visual-current/`，再与 `docs/images/` 的基线逐像素比对；加 `-DUPDATE_BASELINES=ON` 才会改写基线，常规运行不碰它。它使用的固定环境变量是：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_SCREENSHOT_WARMUP='3'
$env:MYRENDERER_MSAA='1'; $env:MYRENDERER_RENDER_PATH='1'
$env:MYRENDERER_PBR='1'; $env:MYRENDERER_IBL='1'; $env:MYRENDERER_SHADOWS='1'
$env:MYRENDERER_BLOOM='0'; $env:MYRENDERER_GRID='0'; $env:MYRENDERER_AXES='0'
$env:MYRENDERER_GROUND='1'; $env:MYRENDERER_SCENE_DEMO='1'
```

七次采集之间只有一组开关不同，每行对应一张基线图；`MYRENDERER_GBUFFER_DEBUG` 的 `5` 就是 `SSAO` 视图：

| 基线图 | `MYRENDERER_SSAO` | `MYRENDERER_TAA` | `MYRENDERER_GBUFFER_DEBUG` | `MYRENDERER_TAA_DEBUG` | `MYRENDERER_TAA_MOTION_DEMO` |
| --- | ---: | ---: | ---: | ---: | ---: |
| `gp_p1d_baseline` | 0 | 0 | 0 | 0 | 0 |
| `gp_p1d_ssao_final` | 1 | 0 | 0 | 0 | 0 |
| `gp_p1d_ssao_debug` | 1 | 0 | 5 | 0 | 0 |
| `gp_p1d_taa_static` | 0 | 1 | 0 | 0 | 0 |
| `gp_p1d_taa_motion` | 0 | 1 | 0 | 0 | 1 |
| `gp_p1d_motion_vectors` | 0 | 1 | 0 | 1 | 1 |
| `gp_p1d_history_weight` | 0 | 1 | 0 | 2 | 1 |

手动重拍单张图时可以只设这一组环境变量，再运行 `build-release/Release/Iris.exe assets/models/pbr_material_test.gltf`；例如 History Weight 诊断图：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_MSAA='1'; $env:MYRENDERER_RENDER_PATH='1'
$env:MYRENDERER_TAA='1'; $env:MYRENDERER_TAA_DEBUG='2'
$env:MYRENDERER_TAA_MOTION_DEMO='1'; $env:MYRENDERER_BLOOM='0'
$env:MYRENDERER_SCREENSHOT='build-release/p1d-history-weight.png'
build-release/Release/Iris.exe assets/models/pbr_material_test.gltf
```

本文「1080p 实测」表的来源是 `screen-space-benchmark`：同一夹具与同一组开关，预热 30 帧、采样 90 帧，把 JSON 写进 `build-release/screen-space-benchmarks/`，文件名是 `gp_p1d_baseline`、`gp_p1d_ssao`、`gp_p1d_taa_static`、`gp_p1d_taa_motion` 与 `gp_p1d_ssao_taa`。这两项效果也能用环境变量在场景加载后覆盖：`MYRENDERER_SSAO`、`MYRENDERER_SSAO_RADIUS`、`MYRENDERER_SSAO_BIAS`、`MYRENDERER_SSAO_STRENGTH`、`MYRENDERER_TAA`、`MYRENDERER_TAA_HISTORY_WEIGHT`（钳制到 0.0～0.98）、`MYRENDERER_TAA_DEBUG`、`MYRENDERER_TAA_MOTION_DEMO`。

## 下一步

1. SSAO 的质量/成本档位：本阶段的 16 样本全分辨率 + 5×5 滤波是按画质验证选的，半分辨率、蓝噪声抖动与时序积累仍未实现，也还没有性能/画质对照数据。这一项尚未写进 `todolist.md`，需要先补成具体条目再动实现。
2. 逐物体 Motion Vector：让 `RenderItem` 真正携带上一帧 Model Matrix、并给实例化批次也提供运动矢量，是上面「独立运动物体被当成静态物体」这条限制的直接修法；该项同样还没有路线图条目。
3. 海面接入 Shadow、Motion Vector、TAA 与调试视图，制作 Calm / Windy / Storm 三组海况——对应 `todolist.md` P1-A 切片 4～5；水面是本阶段 TAA 的第一个真实动态运动物体用例。
4. 共享语义侧后续会为体积云单独生成 volume motion vector，并用低分辨率深度差 + 邻域 min/max clamp 做 history 拒绝（`todolist.md` P1-A 切片 6 的 C4），那时需要重新评估云缓冲与几何 TAA 的耦合成本，在那之前不为假设需求重构现有 TAA。
