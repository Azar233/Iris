# SR-P2A～C：Stylized / NPR（非真实感渲染）收口

- 记录日期：2026-09-16（与 `todolist.md` 第 2.6 节 SR-P2A～C 的完成日期一致；`build-ci-msvc/stylized-benchmarks/` 的两份 JSON 写于同日 21:36）
- 源码 revision：`35a726c`（`feat: complete scene reference and stylized rendering workflows`）
- 构建目录：`build-ci-msvc`，Visual Studio 17 2022，Release，`BUILD_TESTING=ON`；验收产物在 `build-ci-msvc/stylized-acceptance/`，性能 JSON 在 `build-ci-msvc/stylized-benchmarks/`
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0（取自两份 Benchmark JSON 的 `gpu` 字段：`NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 | OpenGL 3.3.0 NVIDIA 591.44`）
- 测量环境：Low 与 High 两档各预热 30 帧、采样 90 帧，VSync 关闭
- 本文引用的验收图：`build-ci-msvc/stylized-acceptance/` 里现存那一套的文件时间是 2026-09-20 20:46～20:48，晚于本阶段的记录日期，说明它们在阶段收口之后被重跑过；图名、采集条件与判定阈值由 `tools/StylizedAcceptance.cmake` 固定，重跑不改变这些图的含义
- 回归基线与验收面背景见 [`regression-baseline-audit.md`](regression-baseline-audit.md)

## 目标与范围

SR-P2A～C 在现有 Scene、材质、灯光、阴影、IBL、Forward 与 Hybrid Deferred 管线上增加一条可切换的 Stylized / Toon（卡通着色）路径。它不是另一套资产系统：同一份 `.myscene`、相机、Transform、glTF 材质和灯光可在 PBR 与 Stylized 之间直接切换，因此风格化在这里是渲染设置，而不是新的内容管线。

本阶段要解决的问题按三条约束收口：

- **复用而不是重写。** 环境 Diffuse Irradiance、局部 Point/Spot Light、Caustics 和已有后处理继续复用。Forward 的透明/透射材质保留现有物理玻璃路径，避免 Toon 分支破坏双界面折射、Beer-Lambert 与色散；Hybrid Deferred 的不透明物体使用同一套量化、高光、边缘光和阴影参数，透明物体仍按既有 Forward Refractive Pass 绘制。
- **默认不改动既有画面。** 所有参数都保存在 `.myscene` 的 renderer 设置中，旧场景没有这些字段时使用默认 PBR 模式，因此现有固定图和场景文件的行为不变。
- **明确不做**：Toon 参数是场景级设置，不是每材质节点图；Face/Direction Map 与玻璃专用 Outline Mask 不在本阶段范围内，这与本阶段「不扩张为通用节点材质编辑器」的范围一致。

## 实现

### SR-P2A：Toon 表面光照、参数与持久化

Inspector 的 `PBR & environment > Shading mode` 提供 `Physically based` 和 `Stylized / toon`。Stylized 模式当前包含：

- `Lighting bands` 与 `Band softness`（也就是 Toon Ramp（卡通阶梯）的分档数与过渡宽度）：把主光和局部光的 `N·L` 量化为 2～8 档，可用窄 `smoothstep` 控制硬边或柔和过渡。
- `Specular size` 与 `Specular softness`：用 `N·H` 阈值生成独立的分层高光。
- `Rim width`、`Rim softness`、`Rim intensity` 和 `Rim color`：根据 `1-N·V` 生成可导演的边缘光，并让背光侧更明显。
- `Shadow tint`：方向光阴影从固定黑色改为可调色调；PCF/彩色透射阴影的可见度仍参与混合。

这些参数按数据流落在三处。持久化侧，`.myscene` 的 renderer 设置逐字段保存它们（`shadingMode`、`stylizedPreset`、`stylizedBandCount`、`stylizedBandSoftness`、`stylizedSpecularSize`、`stylizedSpecularSoftness`、`stylizedRimWidth`、`stylizedRimSoftness`、`stylizedRimIntensity`、`stylizedShadowTint`、`stylizedRimColor`，以及描边、Dither、Height Fog、LUT 与调试视图字段，见 `src/scene/SceneDocument.cpp`）。运行时侧，Inspector 的编辑打包成一条 `EditorCommandType::SetShadingSettings`，由 `EditorDomain::captureShadingSettings` 做范围归一化（`stylizedBandCount` 钳制到 2～8、`stylizedOutlineWidth` 钳制到 0.5～6.0、颜色分量钳制到 0～1）。渲染侧，`Renderer` 把同一份设置同时喂给 Forward 着色器与 `deferred_lighting.frag` 的直接光项（例如 `uStylizedBandCount` 与分层用的 diffuse band），所以两条路径的风格参数不会各自漂移。

### SR-P2B：屏幕空间描边

`Screen-space outline`（屏幕空间描边）在最终合成阶段读取当前帧 Depth；Hybrid Deferred 还会读取 G-Buffer Encoded Normal，以捕获同一物体内部的硬折角。Forward 没有常驻 Normal Buffer，因此使用低成本 Depth-only 回退，不额外执行几何 Prepass。

- `Outline width` 以像素为单位，采样步长由实际输出尺寸计算，调整分辨率时不会按 UV 比例无意变粗或变细。
- `Outline depth threshold` 使用重建后的 View-space 相对深度差，避免直接比较非线性 Depth 带来的远近不一致。
- `Outline normal threshold` 控制 Deferred 法线折角灵敏度。
- 轮廓只向已有不透明几何内部扩张，不在天空一侧产生外部 Halo。
- 描边在 TAA、Bloom、曝光、Tone Mapping 和 sRGB 之后合成，因此不会进入 TAA History 产生拖影，颜色也不随曝光漂移。

BLEND 和玻璃材质沿用不写 Depth 的既有语义，因而不会生成实体外轮廓；它们后方不透明物体的 Depth 轮廓仍然可见。这是当前明确的透明边界规则，不把玻璃错误地当作不透明剪影。未来如需玻璃专用轮廓，应使用独立材质 Mask，而不是改变深度写入。

### SR-P2C：时序稳定 Dither

`Ordered dither`（有序抖动）使用固定在输出像素坐标上的 4×4 Bayer 阈值矩阵，把显示空间颜色有序量化为 16 级；`Dither strength` 在原色与量化结果之间连续混合。实现不读取时间、帧号或随机状态，所以静止画面不会出现噪点闪烁，Forward 与 Deferred 也共享同一最终合成实现。

Dither 在 TAA、Bloom、曝光、Tone Mapping 与 sRGB 编码之后执行，在屏幕空间描边之前执行。因此它不会进入 TAA History，也不会让描边颜色产生网点；当前版本会作用于包括天空和透明合成结果在内的完整显示画面。`Stylized debug > Dither pattern` 可直接显示 Bayer 阈值，不受场景光照影响。Dither 默认关闭，旧 `.myscene` 缺少新增字段时不会改变画面；开关、强度与调试视图均参与场景保存和重新加载。

### SR-P2C：Height Fog 与 Color Grading 3D LUT

`Height fog`（高度雾）根据相机到不透明表面的世界空间线段，对指数高度密度做解析积分。`Fog density`、`Fog base height`、`Fog height falloff` 与 `Fog color` 均可编辑和保存。当前合成合同如下：

1. TAA 先解析 HDR Scene History，Bloom 从解析后的 HDR Scene 提取。
2. Bloom 与 Scene 相加后，在曝光和 Tone Mapping 之前混合线性 HDR Height Fog。
3. 天空没有有限深度，因此不参与 Height Fog；不会把整张 Skybox 洗成雾色。
4. BLEND/玻璃不写 Depth，雾使用其后方不透明表面的深度作用于最终透明合成结果，不把同一段空气重复衰减两次。
5. 曝光、ACES 与 sRGB 之后采样 32³ RGB16F Color Grading LUT，再执行 Dither，最后执行 Outline。

Color Grading 提供 Clean Toon、Painterly、Night Aurora 三张运行时生成并上传的 3D LUT（三维查找表，3D lookup table；`PostProcessor` 里 `colorGradingLutSize = 32`，逐项生成后 `glTexImage3D` 上传），使用硬件三线性采样和 0～1 强度混合。Bloom 继续复用已有 Extract/Blur/Composite 实现；Preset 只切换现有 Bloom 开关、阈值和强度，没有第二套风格化 Bloom。

### SR-P2C：三组 Preset 与固定场景

Inspector 的 `Stylized preset` 可一键切换以下组合；Preset 名称和展开后的每个参数都写入 `.myscene`：

| Preset | 组合重点 | 固定场景 |
| --- | --- | --- |
| Clean Toon | 3 档硬分层、细描边、Clean LUT、无雾/无 Dither/无 Bloom | `15_stylized_clean_toon_gallery.myscene` |
| Painterly | 4 档软分层、暖色 Rim、轻 Dither、Height Fog、Painterly LUT、低强度 Bloom | `16_stylized_painterly_interior.myscene` |
| Night Aurora | 冷色 Shadow、青色 Rim、Dither、浓 Height Fog、Night LUT、较强 Bloom | `17_stylized_night_aurora_outdoor.myscene` |

三类场景分别覆盖材质展台、室内陈列和由现有植物/球体组成的室外自然代理构图，没有引入新的角色资产或外部下载依赖。

### 诊断：`Stylized debug` 的六种视图

`Stylized debug` 提供 Lighting Bands、Rim、Outline、Dither、Fog 与 LUT Delta 六种视图（Inspector 里的下拉项依次是 `Final output`、`Lighting bands`、`Rim factor`、`Outline edges`、`Dither pattern`、`Fog factor`、`LUT delta`，`Final output` 是正常画面）。前两种直接输出表面着色因子，其余在最终合成阶段输出对应屏幕空间证据：Outline 视图显示描边强度本身，Fog 视图显示雾因子本身，Dither 视图显示 Bayer 阈值本身（因此不受场景光照影响），LUT delta 视图显示分级前后差值的放大结果（着色器里按 `abs(graded - color) * 4.0` 输出）。这些视图是低成本的：它们在 `postprocess.frag` 里各自提前返回，不额外增加 Pass。

## 截图

本阶段的画面全部由 `stylized-acceptance` 采集并写入构建目录 `build-ci-msvc/stylized-acceptance/`，共同的采集条件是 `MYRENDERER_SMOKE_TEST=1`、`MYRENDERER_STYLIZED_BANDS=3`、`MYRENDERER_STYLIZED_OUTLINE_WIDTH=1.5` 与 `MYRENDERER_HIDE_SELECTION_OUTLINE=1`，逐图再叠加 Preset、渲染路径、开关与调试视图覆盖。本文内嵌的九张图都在 `14_polyhaven_material_gallery.myscene` 上以 640×360 采集，其中 Stylized 侧统一为 `MYRENDERER_STYLIZED=1` 与 `MYRENDERER_STYLIZED_OUTLINE=1`，只有 PBR 参考一栏是 `MYRENDERER_STYLIZED=0` 加 `MYRENDERER_STYLIZED_OUTLINE=0`。`MYRENDERER_STYLIZED_PRESET` 在 `MYRENDERER_STYLIZED_BANDS` 之前应用，所以 Painterly 一张显式传 `MYRENDERER_STYLIZED_BANDS=4`，其余保持 3。

### 同一机位的三组 Preset

同一机位、同一 640×360、同一灯光与同一 `.myscene` 下依次套用 Clean Toon、Painterly、Night Aurora：分档数、分层软硬、Rim 颜色与强度、描边粗细、Dither、雾与 LUT 全部换掉，而几何、机位与相机参数完全不动。这张对照证明 Preset 改的是画面风格而不是内容，也证明三组 Preset 之间确实互不相同（`stylized-acceptance` 对相邻两档做逐像素比较并要求产生差异）。

| Clean Toon | Painterly | Night Aurora |
| --- | --- | --- |
| ![Clean Toon：3 档硬分层、细描边、无雾无 Dither](../build-ci-msvc/stylized-acceptance/sr_p2c_same_camera_clean_toon.png) | ![Painterly：4 档软分层、暖色 Rim、轻 Dither 与 Height Fog](../build-ci-msvc/stylized-acceptance/sr_p2c_same_camera_painterly.png) | ![Night Aurora：冷色 Shadow、青色 Rim、浓 Height Fog 与较强 Bloom](../build-ci-msvc/stylized-acceptance/sr_p2c_same_camera_night_aurora.png) |

### PBR 与 Stylized 的同机位切换

同一份 `.myscene`、同一机位、同一灯光，只把 `Shading mode` 从 `Physically based` 切到 `Stylized / toon`（Clean Toon Preset）：左栏是连续着色的 PBR 结果，右栏是同几何上的硬分层、分层高光与内描边。这张对照证明「同一资产可直接切换着色路径」不是文案，而是逐像素不同的画面——验收目标正是对这两张图要求必须产生差异。右栏与上一张图的 Clean Toon 一栏是同一个文件。

| PBR（`Physically based`） | Stylized（Clean Toon） |
| --- | --- |
| ![PBR 同机位参考：连续光照与物理高光](../build-ci-msvc/stylized-acceptance/sr_p2a_pbr_forward.png) | ![Clean Toon 同机位：同一几何上的硬分层与内描边，与左栏逐像素不同](../build-ci-msvc/stylized-acceptance/sr_p2c_same_camera_clean_toon.png) |

### 调试视图：分层与描边

两张调试视图分别显示表面着色因子本身，用于在不看最终画面的前提下判断画质来自哪一步。Lighting Bands 视图把主光的 `N·L` 量化结果直接画出来，可以看到 3 档的边界落在曲面上而不是噪声上；Outline 视图只显示描边边沿强度，可以看到轮廓完全位于不透明几何内部，天空一侧没有外部 Halo。两图同样证明「六种调试视图各自产生非空差异」这条验收条件有画面依据。

| Lighting bands | Outline edges |
| --- | --- |
| ![Lighting bands 调试视图：主光 N·L 的 3 档量化边界直接可见](../build-ci-msvc/stylized-acceptance/sr_p2c_debug_lighting_bands.png) | ![Outline edges 调试视图：描边只存在于不透明几何内部，天空一侧无 Halo](../build-ci-msvc/stylized-acceptance/sr_p2c_debug_outline.png) |

### Forward 与 Deferred 的风格化一致性

同一 Height Fog 参数（`density 0.55`、`base height -0.4`、`falloff 0.9`）在 Forward 与 Deferred 两条路径上分别采集：两栏的雾浓度梯度、受雾影响的表面范围与地平线附近的过渡看起来一致。这张对照证明风格化合成没有按渲染路径分叉——`stylized-acceptance` 对这两张图的显示空间差异设了 `MAE <= 0.02` 与变化像素 `<= 15%` 的上限，与 Toon、Dither、LUT 三组 Forward/Deferred 对照用同一套阈值；透明边界单独在 `12_reference_pathtracer_volume.myscene` 上用 `sr_p2b_transmission_boundary` 与 `sr_p2c_fog_transmission_boundary` 一对图验收，本文不重复内嵌。

| Forward | Deferred |
| --- | --- |
| ![Height Fog 在 Forward 路径上的最终合成](../build-ci-msvc/stylized-acceptance/sr_p2c_fog_forward.png) | ![同一 Height Fog 参数在 Deferred 路径上的最终合成，与左栏在同一阈值内](../build-ci-msvc/stylized-acceptance/sr_p2c_fog_deferred.png) |

同一目录下还有三张 Preset 场景的画面（`sr_p2c_preset_clean_gallery.png`、`sr_p2c_preset_painterly_interior.png`、`sr_p2c_preset_night_aurora_outdoor.png`），它们证明三组 Preset 在各自 `.myscene` 上的实际效果；本文不重复内嵌，路径与重拍命令见「复现命令」。

## 验证

- `stylized-acceptance` 是本阶段的主验收口，通过正常场景加载器打开材质展台 `14_polyhaven_material_gallery.myscene`、固定玻璃场景 `12_reference_pathtracer_volume.myscene` 和三组 Preset 场景 `15`/`16`/`17`，输出并检查：`sr_p2a_pbr_forward.png`；640×360 的 Toon Forward / Deferred 及 Outline On/Off；640×360 且 TAA On 时的 Outline On/Off；960×540 的 Outline On/Off；Dither Forward / Deferred、TAA On 与 Dither Debug；同一静态画面在 1 帧和 4 帧预热后的 Dither 捕获；Height Fog、透明边界、三张 Color Grading LUT 与对应调试视图；同一机位 PBR / Clean Toon / Painterly / Night Aurora；三组 `.myscene`；以及 Lighting Bands、Rim、Outline、Dither、Fog、LUT 六种调试视图。
- 验收的判定条件是具体的：PBR/Toon、640×360 Outline On/Off、TAA Outline On/Off 和 960×540 Outline On/Off 均不能逐像素相同；Toon 与 Dither 的 Forward / Deferred 还必须分别满足显示空间 `MAE <= 0.02` 且变化像素比例 `<= 15%`；Dither 开/关、TAA 下开/关及调试视图必须产生差异，而两个不同预热帧的静态 Dither 捕获必须逐像素一致（比较器以容差 `0 0` 调用，逐像素相同才算通过）；Preset、Fog、LUT 和六种调试视图还必须分别产生非空差异。采集或比较失败时脚本报出 `FATAL_ERROR`，因此该 target 失败即非零退出。
- 自动化会隐藏编辑器 Selection Outline（`MYRENDERER_HIDE_SELECTION_OUTLINE=1`，两个脚本都传），避免把橙色选择框误当成 NPR 证据；该比较不会创建或改写现有固定图 baseline，`docs/images/` 与 `docs/reference-images/` 中的图没有被本阶段移动。`docs/regression-baseline-audit.md` 记录 `stylized-acceptance` 在该 revision 下 Pass。
- `stylized-benchmark` 产出本文的性能表：Low 档用 `15_stylized_clean_toon_gallery.myscene` + Preset 1（Clean Toon）、Forward、1× MSAA、1280×720、TAA 关闭；High 档用 `17_stylized_night_aurora_outdoor.myscene` + Preset 3（Night Aurora）、Deferred、4× MSAA、1920×1080、TAA 开启；两档都预热 30 帧、采样 90 帧，JSON 名为 `sr_p2c_low` 与 `sr_p2c_high`。
- `editor-session`（CTest）覆盖着色载荷的往返：提交 `SetShadingSettings` 后 `stylizedBandCount`、`stylizedRimIntensity`、`stylizedDitherEnabled`、`stylizedShadowTint`、`stylizedColorGradingLut` 与 `stylizedDebugView` 按原值读回，因此 Inspector 的改动不是只改了界面状态。
- `scene-document-repeat-load`（CTest）覆盖 `.myscene` 持久化：`shadingMode`、`stylizedPreset`、分档与软度、高光、Rim、描边（含 `stylizedOutlineEnabled = false` 这种关闭态）、Dither、Height Fog 与 Color Grading LUT 在首次加载与重复加载后都保持原值，旧文件缺字段时取默认值。
- 未验证项：`gpu-smoke` 的当前命令列表里没有 stylized 场景（它覆盖 PBR、玻璃、Deferred、大气、灯光压力、实例与恢复路径），因此风格化着色器的真实上下文编译链接是由 `stylized-acceptance` 与 `stylized-benchmark` 自己完成的，而不是 `gpu-smoke`；本文的性能数字只有一台参考机的证据，见「限制与取舍」。

### Low / High GPU 档（`stylized-benchmark`）

固定设备为 NVIDIA GeForce RTX 4060 Laptop GPU、OpenGL 3.3、驱动 591.44；每档 30 帧预热、90 帧采样，VSync 关闭。表里测的是**完整最终合成**（含 Preset 打开的描边、Dither、Height Fog、Color Grading LUT 与 Bloom 组合）的整帧 GPU / CPU 帧时间、Draw Call 数与代码侧显存估算。原始 JSON 位于 `build-ci-msvc/stylized-benchmarks/`：

| 档位 | 配置 | GPU P50 / P95 | CPU P50 / P95 | Draw Call | Render memory |
| --- | --- | ---: | ---: | ---: | ---: |
| Low | 1280×720、Forward、1× MSAA、Clean Toon | 0.895 / 0.909 ms | 2.685 / 3.525 ms | 15 | 175.7 MiB |
| High | 1920×1080、Deferred、4× MSAA、TAA、Night Aurora 全组合 | 2.181 / 2.620 ms | 3.211 / 4.246 ms | 55 | 636.1 MiB |

两档的差距来自三处叠加：分辨率从 1280×720 升到 1920×1080、MSAA 从 1× 升到 4×、以及 High 档把 Dither、浓 Height Fog、较强 Bloom 与 TAA 全部打开；Low 档是廉价的发布档，只用 Forward + Clean Toon，不含 TAA。

表的重拍命令：

```powershell
cmake --build build-ci-msvc --config Release --target stylized-benchmark
```

## 限制与取舍

- **Toon 参数是场景级设置，不是每材质节点图。** 这是本阶段最有意接受的边界：同一个场景里所有不透明物体共享一套量化、高光、Rim 与阴影参数，因此无法给单个角色或单件道具单独调参。Face/Direction Map 与玻璃专用 Outline Mask 同样不在本阶段范围内，与该阶段「不扩张为通用节点材质编辑器」的范围一致。
- **玻璃与 BLEND 材质不会生成实体外轮廓**，只有它们后方不透明物体的 Depth 轮廓可见。这是被接受的透明边界规则；要玻璃专用轮廓必须使用独立材质 Mask，而不是改变深度写入。
- **Dither 作用于完整显示画面**，包括天空与透明合成结果；也就是说它无法只作用于不透明几何。这是当前版本的行为而不是缺陷，但意味着天空的渐变带也会被量化。
- **天空不参与 Height Fog**，因为它没有有限深度；把 Skybox 一起洗成雾色是被明确排除的做法。
- **Forward / Deferred 的风格化一致性是阈值内的，不是逐像素相等**：验收只要求显示空间 `MAE <= 0.02` 且变化像素 `<= 15%`，所以两条路径之间仍允许边缘 MSAA Resolve 与量化细节上的差异。
- **本文内嵌的九张图都在构建目录里，不在版本库中**：Markdown 里的路径是相对 `docs/` 的 `../build-ci-msvc/stylized-acceptance/...`，必须先跑 `stylized-acceptance` 才会存在；它们既不是 `docs/images/` 的回归基线，也没有被复制进 `docs/media/`。清除构建目录会让这些图在 Markdown 里失效，重跑目标即可恢复。
- **性能数字只有一台参考机的证据**：`0.895 / 0.909`、`2.685 / 3.525`、`2.181 / 2.620`、`3.211 / 4.246 ms` 与 `175.7 / 636.1 MiB` 都来自同一台 NVIDIA GeForce RTX 4060 Laptop GPU（驱动 591.44，OpenGL 3.3.0），没有跨机器或跨驱动的对照。
- **表里的 Render memory 是代码侧估算，不是 GPU 实测分配**：它来自 `Renderer::estimatedRenderMemoryBytes()` 对各子系统 `estimatedBytes()` 的求和，不含驱动簿记与分配器填充。
- **待复核（UI 标签与源文档用词）**：原始阶段文档在正文里把六种调试视图写作「Lighting Bands、Rim、Outline、Dither、Fog 与 LUT Delta」，把 LUT 功能整体称作「Color Grading」；当前工作树里的实际 Inspector 标签是 `Lighting bands`、`Rim factor`、`Outline edges`、`Dither pattern`、`Fog factor`、`LUT delta`，LUT 控件是 `Color grading LUT` 开关、`Color LUT` 下拉（`Clean Toon` / `Painterly` / `Night Aurora`）与 `LUT strength`。本文按实际标签书写，功能与数量的描述没有改动；这一处用词差异未向原作者确认。
- **待复核（Low 档没有 TAA）**：表中 Low 档写的是「Forward、1× MSAA、Clean Toon」，对应 `tools/StylizedBenchmark.cmake` 里 `MYRENDERER_TAA=0`；原文没有把 TAA 关闭写进这一行的配置描述，本文在正文里点明，表格文字保持原样。

## 复现命令

```powershell
cmake --build build-ci-msvc --config Release --target stylized-acceptance
cmake --build build-ci-msvc --config Release --target stylized-benchmark
```

`stylized-acceptance` 就是本文九张图的来源：它按固定环境变量重拍整套画面到 `build-ci-msvc/stylized-acceptance/`，逐对比较并在失败时非零退出；常规运行不创建、也不改写 `docs/images/` 的固定图。它的环境变量骨架是下面这一组，其中渲染路径、TAA、分辨率、调试视图与 Preset 逐图不同：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_STYLIZED='1'
$env:MYRENDERER_STYLIZED_BANDS='3'
$env:MYRENDERER_STYLIZED_OUTLINE='1'
$env:MYRENDERER_STYLIZED_OUTLINE_WIDTH='1.5'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_RENDER_PATH='1'   # 0 = Forward，1 = Deferred (hybrid)；PBR 参考一栏为 0
$env:MYRENDERER_TAA='0'           # 0/1
$env:MYRENDERER_RENDER_WIDTH='640'; $env:MYRENDERER_RENDER_HEIGHT='360'
```

手动重拍本文某一张图时可以只设这一组再加该图自己的覆盖项，例如同一机位的 Night Aurora：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_STYLIZED='1'; $env:MYRENDERER_STYLIZED_PRESET='3'
$env:MYRENDERER_STYLIZED_BANDS='3'
$env:MYRENDERER_STYLIZED_OUTLINE='1'; $env:MYRENDERER_STYLIZED_OUTLINE_WIDTH='1.5'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_RENDER_PATH='1'; $env:MYRENDERER_TAA='0'
$env:MYRENDERER_RENDER_WIDTH='640'; $env:MYRENDERER_RENDER_HEIGHT='360'
$env:MYRENDERER_SCREENSHOT='build-ci-msvc/stylized-acceptance/sr_p2c_same_camera_night_aurora.png'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/fixtures/14_polyhaven_material_gallery.myscene
```

自动化也可单独覆盖场景中保存的模式：`MYRENDERER_STYLIZED=0|1`、`MYRENDERER_STYLIZED_BANDS=2..8`、`MYRENDERER_STYLIZED_OUTLINE=0|1`、`MYRENDERER_STYLIZED_OUTLINE_WIDTH=0.5..6.0`、`MYRENDERER_STYLIZED_DITHER=0|1`、`MYRENDERER_STYLIZED_DITHER_STRENGTH=0..1`、`MYRENDERER_STYLIZED_FOG=0|1`、`MYRENDERER_STYLIZED_FOG_DENSITY=0..2`、`MYRENDERER_STYLIZED_FOG_BASE_HEIGHT=-10..10`、`MYRENDERER_STYLIZED_FOG_FALLOFF=0.01..4`、`MYRENDERER_STYLIZED_COLOR_GRADING=0|1`、`MYRENDERER_STYLIZED_LUT=0..2`、`MYRENDERER_STYLIZED_LUT_STRENGTH=0..1`、`MYRENDERER_STYLIZED_PRESET=0..3`、`MYRENDERER_STYLIZED_DEBUG=0..6` 和 `MYRENDERER_RENDER_PATH=0|1`。这些覆盖在场景加载后应用，仅用于可重复捕获；`MYRENDERER_STYLIZED_PRESET` 先应用，随后逐个字段的覆盖（例如 `MYRENDERER_STYLIZED_BANDS`）再覆盖 Preset 展开的值。

## 下一步

1. SR-P2A～C 已完成表面光照、屏幕空间描边、Dither、Height Fog、3D LUT、三组 Preset、六种调试视图和 Low/High 性能证据，对应 `todolist.md` 第 2.6 节与 P0-B「收口 Stylized/NPR SR-P2C」的全部条目；本文即该阶段的证据文档。
2. 若要把风格化推到角色级控制，需要先把「每材质 Toon 参数」或「Face/Direction Map」写成 `todolist.md` 里的具体工作包——本阶段有意不做，且新增逐材质参数会影响 `.myscene` 的材质架构，不能当成纯 UI 改动。
3. 玻璃专用 Outline Mask 是「透明边界」这条限制的直接修法：用独立材质 Mask 提供描边形状，而不是让玻璃写 Depth；该项同样还没有路线图条目。
