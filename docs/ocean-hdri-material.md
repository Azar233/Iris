# P1-A 开放海域：现有天空盒与可调海面材质

> 日期：2026-10-03。源码：`ac9193b` 基础上的当前工作区。
> 构建：`build-ci-msvc` Release、`build-mingw` Debug。
> GPU：NVIDIA GeForce RTX 4060 Laptop GPU；OpenGL 3.3.0 NVIDIA 591.44。

## 目标与范围

针对低视角海面截图中天空与水面之间的整片灰色背景，改用仓库已打包的 Poly Haven Kloofendal HDRI 作为开放海域场景的天空盒和水面反射源，延长可见海面，并提供可在 Inspector 中调节的海面材质。体积云算法保持原样；原有云海场景移入自动验收夹具。

## 实现

### 场景与资源

原相机远裁剪面为 `100` 世界单位；`waterExtent=500` 并不能让被裁掉的海面继续显示。`02_ocean_weather_hero.myscene` 现在显式保存 `farPlane=1600` 与 `waterExtent=2000`，并关闭解析大气和云。原有 Kloofendal 4K OpenEXR 仍是旧场景的默认天空；Hero 选择新增的 Overcast Soil 2K 纯天空 OpenEXR，以获得更多阴天云层细节。两份资源的来源、作者、校验值和 CC0 许可见 [`assets/environments/README.md`](../assets/environments/README.md)。镜面反射和背景取同一份环境贴图。开放海域不再放置 `Deep Seabed`；带礁石、体积云和海底的旧配置保存在 `assets/scenes/fixtures/23_ocean_clouds.myscene`，既有云影、光束和确定性作业改用该夹具。

调研了 [osgw](https://github.com/CaffeineViking/osgw) 的 MIT 许可 OpenGL Gerstner 海面实现。其整个渲染器依赖 OpenGL 4.1 Tessellation，不适合直接替换本项目的 OpenGL 3.3 管线；本次复用其中水面材质使用的 3D Simplex noise 源码作为多尺度细波法线，保留已有 Gerstner 几何、透射、阴影、TAA 与 Forward/Deferred 合成。源码与原作者许可分别在 `shaders/ocean_snoise.glsl`、`assets/licenses/osgw-MIT.txt` 和 `assets/licenses/ashima-webgl-noise-MIT.txt`。水体颜色与泡沫仍由现有程序化材质生成，不另引入一张固定颜色贴图。

### 材质与编辑器

细波只改变着色法线，不改变几何与运动矢量。此前两层细波在距相机 15～100 单位间同时淡出，使近景偏密、远景偏平。现在两层改为较宽的 `0.065` 与较细的 `0.28` 世界空间频率，分别根据片元的世界空间像素足迹（`fwidth`）逐级过滤：当某层波纹已无法由屏幕像素稳定分辨时才减弱，而较宽的波纹可继续延伸到中远景。这是屏幕空间抗锯齿近似，不会凭空增加远处几何细节。环境贴图的预滤波 mip 由 `waterRoughness` 控制；`waterReflectionStrength`、`waterRippleStrength`、`waterSunGlintStrength` 分别控制天空反射、细波扰动和定向光高光。四项均进入 `.myscene`、`EditorDomain`、`SetWaterSettings` 校验、Renderer uniform 与 Inspector 的 `Water surface` 分组；旧场景使用保持原有外观的默认值。`Camera` 分组提供 100～2000 的远裁剪距离，场景文件可逐项往返；旧场景未声明时仍为 100。原有 `Post processing` 分组已有 SSAO 开关、半径、偏移和强度，仍适用于 Deferred 的不透明物；它不作为海水材质自身的 AO 参数。

### 无海底开放海域

Inspector 的 `Lighting & environment → Sky environment` 可在局部多云、阴天和晴天 HDRI 间切换；该选择进入 `.myscene`、Forward/Deferred 环境贴图和 CPU 路径追踪，并使 CPU 预览失效后重新采样。晴天使用 Kloofendal 43d Clear 的纯天空 HDRI。旧场景缺少该字段时选局部多云。切换 HDRI 或从解析大气返回 HDRI 时重建现有环境立方体贴图；重建复用 GL 纹理对象，避免每次切换额外分配纹理。Hero 初值使用阴天预设、`environmentIntensity=0.55`、`exposure=0.68` 和 `waterReflectionStrength=0.52`。这些值用于偏暗的阴天展示，不改变旧夹具的默认值；切换晴天后仍可通过已有环境强度和曝光控件调节亮度。

删除深海床后，旧折射路径会把天空盒当成水下背景，使海水明显变亮、偏青。在没有不透明物深度的区域，新增 `waterDeepWaterStrength` 将该天空折射逐步替换为深水辐亮度；旧场景默认 `0`，Hero 设为 `1`。Inspector 的 `Deep water` 滑块可实时调节，场景文件保存该值。仍有水下物体时保留原有深度折射和吸收。

近景反射由两层噪声法线扩展为四层世界空间尺度，新增高频层只在投影像素能解析时启用，并按法线像素方差增加反射粗糙度，减轻细波高光的孤立闪点。Hero 的粗糙度、反射和细波值随固定机位截图重新调整；这只是现有 Gerstner 海面的一轮材质改进，不等同于参考图中的频谱海面或真实半影。

进一步诊断 Hero 海面的零散近白色斑点时，分别关闭泡沫、太阳高光、环境反射，并显示水面使用的环境漫反射探针，最终定位到开放深水底色的 `uIrradianceMap` 按逐像素波面法线采样。该低分辨率漫反射探针中的极亮值随法线形成孤立斑块；它们不是物体投影，也不是抗锯齿造成的。没有不透明折射接收物时，深水底色现在以朝上的方向采样环境漫反射，保留波面法线驱动的天空反射；有水下物体的深度透射路径不变。没有可见投影物体时，Renderer 同时跳过无意义的物体阴影图绘制与采样；添加方块后仍按原路径生成阴影。

### 多方向几何波

Hero 的 `waterWaveDiversity=1` 在 High 档保留原四组波的基础上，加入波长 `21.7 / 11.2 / 5.4 / 2.5` 世界单位的四组非整数比例波，并减弱原主波的占比。世界空间的低频相位弯曲使波峰方向缓慢变化；CPU `WaterWaves::evaluate()` 与 GPU 顶点着色器使用相同的波形、相位和解析导数，当前帧与上一帧位置由同一函数计算。已有场景默认 `0`，仍只计算原 High 四组或 Low 两组；Inspector 的 `Wave diversity` 可以连续调节。网格边长过滤继续削弱远处无法解析的短波。

这一步属于更丰富的 Gerstner 波形合成，没有实现 Tessendorf/FFT 频谱，也没有将法线细波误当作几何起伏。

### 近景网格聚焦

原二次网格映射在 Hero 的 `waterExtent=2000` 下，距相机约 `5 / 20 / 100` 世界单位处的 High 单格边长分别约为 `2.30 / 4.38 / 9.53`。因此 `2.5` 单位短波在近处也被网格边长过滤。新增 `waterNearMeshFocus` 在二次映射与连续的指数映射之间插值；Hero 设为 `1`，旧场景默认 `0`。不增加顶点和 draw call，而是将固定网格更多地分配给近景；相同三个距离的估算单格边长变为 `0.49 / 1.82 / 8.87`。GPU 的短波边长过滤随映射同步计算，Inspector 的 `Near mesh focus` 可实时调节并保存到场景。

指数映射仍保持相机中心、边界和世界空间波形连续。它把远处单格变得更大，因此远景只保留能被网格解析的长波；本次没有引入重叠网格或尚未验证的拼缝处理。

## 截图

初版 1072×559 开放海域画面中，现有 HDRI 天空盒和延长的海面在地平线连续交接，没有用户截图中 125～175 行的整片纯色灰带；近景反射仍可通过材质滑块调整。下方首图是移除海底前的阶段记录。

![开放海域的 HDRI 天空盒与连续海面](media/p1a-ocean-hdri-open.png)

移除海底并启用深水底色后的同机位画面如下。该图证明场景可以在 `0` 个实体时渲染和导出，海面保持深蓝且不再存在可选中的大海底板。

![无海底开放海域与深水底色](media/p1a-ocean-open-deep-water.png)

同一机位、曝光、动画时间和 `4× MSAA` 下，开启几何波形丰富度后，中景波峰不再沿画面保持同样笔直的长条。画面仍保留可见的方向性，不能以此宣称达到参考图的海况统计。

![多方向几何波开启后的开放海域](media/p1a-ocean-wave-diversity.png)

与上图关闭该参数的画面对比，`MyRendererImageComparison` 测得 MAE `0.02784`、变化像素 `34.40%`；该指标只证明参数改变了画面，不评价照片真实感。两图均为 `1072×559`，固定时间 `1.25 s`。

保持八组波和相同机位，仅开启近景网格聚焦后，原先被过滤的短几何波可进入近景位移与反射。对照上一图的 MAE 为 `0.02051`、变化像素 `27.86%`；画面没有出现可见的网格拼缝。

![近景网格聚焦后的开放海域](media/p1a-ocean-near-mesh-focus.png)

同一 `1072×559` 机位、`1.25 s` 动画时间与 `4× MSAA` 下，稳定深水环境光移除了原先散落的近白色斑点，天空像素保持一致。以海面区域 RGB 三通道均大于 `245` 为计数条件，修正前 `79` 个像素，修正后 `0` 个；这是该类斑点的定点检查，不代表已达到参考照片的整体材质质量。

![稳定深水环境光后的开放海域](media/p1a-ocean-stable-deep-ambient.png)

同机位替换为 Overcast Soil 阴天 HDRI 并调整 Hero 初值后，天空具有连续的云层纹理，海面与地平线的色调更一致。海面的大面积条带状高光仍然可见；天空素材不能代替后续的波谱和微法线改进。

![阴天 HDRI 与开放海域](media/p1a-ocean-overcast-environment.png)

同场景仅将 `environmentPreset` 从 `1` 改为 `2` 的晴天对照如下。背景、环境反射和 CPU 路径追踪均选择晴天资源；波浪和其它海面参数保持相同。

![晴天 HDRI 与开放海域](media/p1a-ocean-clear-environment.png)

原截图的 125～175 行中央区域 RGB 标准差为 `0/0/0`；初版图同一区域为 `23.75/14.32/5.99`，仅用于佐证纯色带消失，不作为画质评分。下方复现命令生成当前无海底画面。

## 验证

- MSVC Release 与 MinGW Debug 完整构建通过；`ctest --test-dir build-ci-msvc -C Release --output-on-failure` 为 `26/26`。
- `scene-document-repeat-load` 验证新增材质参数、2000 范围内海面与相机远裁剪往返；`camera-navigation` 验证局部移动不修改远裁剪；`water-wave-synthesis` 保持原有 CPU/GPU 波浪合同。
- GPU screenshot 在 OpenGL 3.3、4×MSAA 下成功输出 1072×559 的开放海域画面；另用临时 `25°` 俯视机位检查近中景衔接，没有改动正式场景相机。`gpu-smoke`、`water-synthesis-acceptance` 与聚焦 `water-wave-synthesis` 均通过。原有云测试已改指向保留的云海夹具。
- 无海底版本在 MSVC Release 与 MinGW Debug 构建成功，完整 CTest `26/26`、`gpu-smoke` 和 `water-synthesis-acceptance` 通过。`scene-document-repeat-load` 验证深水强度往返及 Hero 零实体；固定 `1072×559`、`4× MSAA` 截图成功。
- Hero 在 `1280×720`、预热 `4` 帧、测量 `30` 帧时，透明/折射 pass 的 GPU P50 为 `1.046 ms`；同场景关闭细波为 `0.886 ms`。两次测量的整帧计时波动较大，这组数只反映本机这次细波开关的成本量级，不作为稳定性能结论。
- 新增波形通过 MSVC Release、MinGW Debug 构建，完整 CTest `26/26`，MinGW 聚焦测试 `2/2`，真实 `gpu-smoke` 和 `water-synthesis-acceptance`。`water-wave-synthesis` 用空间差分检查相位弯曲后的解析法线，并用时间差分检查速度；旧场景保持 `waterWaveDiversity=0`。
- Hero 在 `1280×720`、预热 `4` 帧、测量 `30` 帧的两组开关计时中，透明/折射 pass GPU P50 分别为开启 `2.00 / 2.23 ms`、关闭 `1.99 / 2.38 ms`。运行间波动大于开关差异，因此尚不能给出可靠的增量成本结论。
- 旧夹具的 `water-synthesis-benchmark` 通过；`1280×720`、`4× MSAA` 下 High 的折射阶段 GPU P95 为 `1.156 ms`，低于该目标的 `2 ms` 预算。该夹具默认关闭新参数，验证旧质量档没有预算回归。
- 近景网格聚焦通过 MSVC Release 完整 CTest `26/26`、MinGW Debug 聚焦测试 `2/2`、`gpu-smoke`、`water-synthesis-acceptance` 和 `water-synthesis-benchmark`。后者旧 High 夹具在 `1280×720`、`4× MSAA` 下折射阶段 GPU P95 为 `1.157 ms`，低于 `2 ms` 门槛。Hero 开关各测预热 `16` 帧、测量 `60` 帧；折射阶段 P50 为开启 `0.32 ms`、关闭 `0.28 ms`，P95 受运行波动影响较大，不据此估算稳定开销。
- 稳定深水环境光修复通过 MSVC Release 完整 CTest `26/26`、MinGW Debug 构建、`gpu-smoke`、`water-synthesis-acceptance` 与 `shadow-cascade-acceptance`。后两项覆盖旧海况/水下与有投影物体的级联阴影；另以 `25_ocean_cube_shadow.myscene` 截图确认方块仍在海面形成阴影。固定 Hero 的天空前 `250` 行逐像素不变，海面近白斑点计数从 `79` 变为 `0`。
- 阴天 HDRI 预设通过 MSVC Release、MinGW Debug 构建、CTest `26/26`、`gpu-smoke` 和 `water-synthesis-acceptance`；`scene-document-repeat-load` 验证旧场景默认值和 Hero 预设往返。固定机位 `1072×559`、`4× MSAA` 截图检查阴天云层与海面交界。该图是视觉对照，不代表完成海面写实质量验收。
- 晴天预设通过 MSVC Release、MinGW Debug 构建及 CTest `26/26`；编辑器交互回归验证切换到预设 `2`，场景文档测试验证保存/加载预设 `2`。真实 OpenGL 截图在同一 Hero 相机、海浪参数下显示不同的晴天天空与反射。晴天沿用 Hero 的阴天曝光和环境强度，用户可通过原有滑块进一步调整亮度。

## 限制与取舍

此阶段改善地平线连续性、材质控制和细波的近远过渡，不声称达到照片级海面。对照用户给的参考图，中景长波的重复感虽减弱，近景短几何波已有更多采样，但形态和能量分布仍与风驱动海浪有差距；Hero 的八组 Gerstner 波仍非真实海浪频谱，细波仍只改变法线。后续水下与接物边界排查已增加几何短波边长过滤、折射候选深度检查和未偏移接触泡沫，但仍不能用单张深度图重建遮挡后的物体；证据与分层网格方案见 [`ocean-underwater-boundaries.md`](ocean-underwater-boundaries.md)。没有 FFT 频谱、真实远海多尺度统计、稳定的屏幕空间或平面反射，也没有独立的水线折射 pass。更远的裁剪面降低传统深度缓冲的远处精度，主要供开放海域场景使用。HDRI 光源方向与手动方向光未自动反求一致；用户切换到解析大气后应重新检查定向高光。SSAO 仍不直接作用于透明海面，水面主要依靠深度透射、环境反射和阴影表现接触关系。

## 复现命令

```powershell
cmake --build build-ci-msvc --config Release --parallel 6
ctest --test-dir build-ci-msvc -C Release --output-on-failure
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_SCREENSHOT='docs/media/p1a-ocean-overcast-environment.png'
$env:MYRENDERER_RENDER_WIDTH='1072'
$env:MYRENDERER_RENDER_HEIGHT='559'
$env:MYRENDERER_ANIMATION_TIME='1.25'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/02_ocean_weather_hero.myscene
```

旧图对照：复制 Hero 场景到构建目录，只将 `waterNearMeshFocus` 改为 `0.0`，可重拍上方八组波的阶段图；同时将 `waterWaveDiversity` 改为 `0.0`，可重拍无海底初版。两份对照文件仅用于本机构建目录的验证，不进入场景清单。

## 下一步

继续 [`todolist.md`](../todolist.md) 的 P1-A 海面写实质量工作：多尺度波谱、微法线与粗糙度统一、稳定反射；体积云改造单独立项，不与本次海面修复混做。
