# SR-P0 场景渲染共用基础（Scene Rendering Foundation）

- 记录日期：2026-09-02（首次入库 `d11f4e7`）；本文最后一条内容变更在 `c50f071`（2026-09-03，补入 SR-P1A 的接口段落）
- 源码 revision：`c50f071`；仓库当前 HEAD 为 `35a726c`，文中提到的类型、Pass 名与 target 在该 revision 中仍然存在；本文嵌入的基线图在 2026-09-16 的 P0-A 基线重锚定中随 Kloofendal EXR 重拍（`sr_p0_scene_entities` 与 `sr_p0_skin_motion` 有变化，`sr_p0_object_motion` 未变），那一轮改动仍是未提交的工作树改动，依据见 [`regression-baseline-audit.md`](regression-baseline-audit.md)
- 构建目录：`build-release`；`README.md` 与本页的常用命令都使用 `build-release`，回归 target 与具体构建树无关，`build-ci-msvc` 同样可用
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0；基线图采集分辨率 1920×1080，阈值 MAE `0.02`、变化像素 `0.10`

## 目标与范围

SR-P0 把原先偏单模型 Viewer 的结构收口为可供实时光栅、风格化渲染和 Reference Path Tracer 共用的场景基础：一个轻量 Scene/Entity 层级、一套显式化状态的 Pass 编排、一套失败安全的 Shader 热重载，以及一套能同时服务相机、刚体与蒙皮几何的运动数据。实现刻意保持轻量：它**不是**完整 ECS，也**不是**通用 Render Graph。

范围上明确不做的事写在「限制与取舍」：没有资源别名与自动依赖推导、没有 Include 依赖追踪、没有每帧精确的蒙皮 AABB，也没有把 Scene 序列化成场景文件。

## 实现

### 持久化：Scene 是运行时内存模型，持久化仍然只覆盖渲染器设置

本阶段首次把「渲染器设置」与「场景对象」分成两层：`.myscene` 只保存前者，Scene 图只活在进程内。这也是后面 SR-P1A 必须补上只读 `SceneSnapshot` 的原因——Path Tracer 需要一个能表达实例、材质、纹理、相机与灯光的共享视图，而不是直接读 `Scene`。

`Scene` 只保存运行时内存状态（`createEntity` / `createEntityWithId` / `duplicateEntity` / `destroyEntity` / `setParent` / `clear` / `find` / `entities` / `beginFrame` / `updateWorldTransforms` / `buildRenderItems`），仓库里没有把 Entity 层级写进文件的路径。既有的 `SceneDocument` 与 `.myscene` 往返保存的是 `RendererSettings`（渲染器、后处理、玻璃、焦散、大气等字段）与 `playback` 参数，不保存 Scene 图；因此「Scene 面板里搭好的对象层级」在重启后不会自己回来。这一点在本阶段是刻意保留的状态，不是遗漏。

### 运行时：场景与资产语义

`Scene` 持有稳定的 `SceneEntityId`（`std::uint64_t`，`invalidSceneEntityId` 为 0）列表。每个 Entity 包含名称、父节点、局部 TRS、资产归一化矩阵、可见性、阴影标记、材质 Tint 实例，以及当前/上一帧世界矩阵。父子关系拒绝循环（`setParent` 会检查 `isDescendant`）；删除父对象时子对象回到根层级。

场景每帧先用 `beginFrame()` 固化上一帧世界矩阵，再计算层级世界矩阵并生成 `RenderItem`。同一个 `GpuModel` 可以被多个 Entity 引用，10 物体固定场景因此只上传一份 Cube 几何。Scene 面板支持选择、显隐、父级切换、复制和删除；复制后的 Transform 与 Tint 独立。

Assimp 导入的静态 glTF 不再把 Node Transform 烘焙进 Vertex。`ModelNodeData` 保存层级，`GpuModel::DrawCommand` 保存节点变换，多个 Node 指向同一 Mesh 时复用同一个 VAO/VBO/EBO。骨骼资产暂时保留兼容旧 Palette 的绑定变换策略，避免在 SR-P0 同时改写蒙皮空间约定。

### 渲染：Pass Context 与状态隔离

`RenderPassContext` 为每个顶层 Pass 记录名称、输入、输出、Viewport、Clear Mask 与 `RenderState`。`OpenGlStateCache` 集中缓存 Depth、Blend、Cull、Front Face 和 Polygon Mode。每个 Pass 进入前应用明确基线状态，结束后恢复默认状态，从而避免透明、焦散或 Debug Pass 把隐含 OpenGL 状态泄漏给后续阶段。

这仍是顺序执行器：资源生命周期和依赖不由系统自动推导。等 Path Tracer、NPR、水体和体积效果出现真实跨队列/跨分辨率需求后，再根据证据决定是否升级 Render Graph。

### 运行时：Shader 热重载

Renderer 每 15 帧检查一次 Vertex/Fragment Shader 的最后修改时间。发生变化时先构建候选 Program；只有所有 Stage 编译且 Link 成功，才与正在使用的 Program 交换。失败时旧 Program 继续渲染，Inspector 显示完整日志；文件再次变化后才重试。成功重载会重置 Temporal History（`previousViewProjectionValid_` 置假），避免新旧 Shader 输出在 TAA 中混合。

### 渲染：Temporal 与动态几何

G-Buffer 的第四个 `RGBA16F` Attachment 保存对象运动向量和有效位（`gbuffer.frag` 的 `gMotion = vec4(currentUv - previousUv, 1.0, 0.0)`）。Vertex Shader 同时接收当前/上一帧 View-Projection、Model、Node 与 Joint Palette（`uCurrentViewProjection`、`uPreviousViewProjection`、`uModel`、`uPreviousModel`、`uNodeTransform`、`uJointMatrices`、`uPreviousJointMatrices`）：

- 静态对象继续使用深度重建的相机 Motion Vector；
- 刚体对象使用当前/上一帧 Model Clip Position；
- 蒙皮对象额外使用上一帧 Joint Palette；
- 首帧、模型切换、尺寸/路径变化或 Shader 重载会使 History 失效（`uMotionHistoryValid` 与 `uPreviousSkinningValid` 共同决定 `vMotionValid`）。

TAA 优先读取有效的对象 Motion Vector（`temporal_aa.frag` 里 `objectMotion.z > 0.5` 时用 `previousUv = vUv - objectMotion.xy`），否则回退到原有相机重建路径。蒙皮对象的包围球在绑定姿势半径基础上加入旋转弧与最大 Joint 位移的保守余量；它牺牲一些剔除精度，换取动画不被错误剔除。

### 诊断：GPU Pass 标签与逐 Pass 时间

每个顶层 Pass 都带一段 `KHR_debug` 调试区间（`glPushDebugGroup`，名称即 `RenderPassContext::name`）与一对时间戳查询。GUI 在每个活动 Pass 旁列出最近一次平滑后的耗时，Benchmark JSON 按 Pass 名给出 P50/P95，因此「哪一段变慢了」可以直接读出来。

## 截图

### 至少 10 个 Entity 共享一份 Cube Mesh

固定机位、1920×1080 下，同一份 `cube.obj` 被 10 个可见 Entity 复用（主对象与 9 个 `MYRENDERER_SCENE_FOUNDATION_DEMO` 实例；对照实例默认关闭），每个 Entity 各自带位置、旋转、缩放与 Tint：画面里能看到一排不同颜色、不同朝向的立方体，而它们背后只有一份几何与一份 `GpuModel`。这张图证明 Entity 级 TRS/Tint 与共享 Mesh 引用真的解耦了，而不是每个对象各复制一份模型。

![SR-P0 场景实体基线：10 个可见 Entity 共享一份 Cube Mesh，只有 TRS 与 Tint 各自独立](images/sr_p0_scene_entities.png)

复现：`cmake --build build-release --target foundation-visual-regression` 会按下面的固定变量重拍这一张（以及两张运动向量调试图）并与 `docs/images/` 的基线逐像素比对：

```powershell
cmake --build build-release --target foundation-visual-regression
```

该 target 的固定环境变量是 `MYRENDERER_SMOKE_TEST=1`、`MYRENDERER_RENDER_WIDTH=1920`、`MYRENDERER_RENDER_HEIGHT=1080`、`MYRENDERER_HIDE_SELECTION_OUTLINE=1`、`MYRENDERER_SCREENSHOT_WARMUP=4`、`MYRENDERER_MSAA=1`、`MYRENDERER_RENDER_PATH=1`、`MYRENDERER_PBR=1`、`MYRENDERER_IBL=1`、`MYRENDERER_SHADOWS=0`、`MYRENDERER_BLOOM=0`、`MYRENDERER_GRID=0`、`MYRENDERER_AXES=0`，再按行追加下表里的一列；`MYRENDERER_SCREENSHOT` 指向目标文件，程序参数是夹具路径：

| 基线图 | 夹具 | 覆盖的环境变量 |
| --- | --- | --- |
| `sr_p0_scene_entities` | `assets/models/cube.obj` | `MYRENDERER_SCENE_FOUNDATION_DEMO=1`、`MYRENDERER_GROUND=0`（见「验证」的**待复核**） |
| `sr_p0_object_motion` | `assets/models/cube.obj` | `MYRENDERER_OBJECT_MOTION_DEMO=1`、`MYRENDERER_TAA=1`、`MYRENDERER_TAA_DEBUG=1` |
| `sr_p0_skin_motion` | `assets/models/skinning_test.gltf` | `MYRENDERER_ANIMATION_DEMO=1`、`MYRENDERER_ANIMATION=1`、`MYRENDERER_ANIMATION_FRAME_STEP=0.18`、`MYRENDERER_TAA=1`、`MYRENDERER_TAA_DEBUG=1` |

### 刚体运动向量：逐对象上一帧变换是否成立

`sr_p0_object_motion` 是同一套固定变量下打开 `MYRENDERER_TAA_DEBUG=1` 的输出，把 Temporal Resolve 写出的 motion vector 直接显示出来：刚体对象的位移在这里来自「当前帧 Model Clip Position 减上一帧 Model Clip Position」，因此它能证明 `RenderItem::previousModelMatrix` 这条数据链真的被喂进了着色器，而不是只有相机重投影在起作用。该图与 `sr_p0_scene_entities` 属于同一套件（`foundation-visual-regression`），没有单独的重拍命令。

### 蒙皮运动向量：本页不内嵌

蒙皮一侧的基线 `sr_p0_skin_motion` 用 `skinning_test.gltf` 与 `MYRENDERER_ANIMATION_FRAME_STEP=0.18` 采集，语义与 `sr_p0_object_motion` 相同，但额外依赖上一帧 Joint Palette 与蒙皮包围球这两条只在 `docs/gpu-skinning.md` 里展开的合同。本文只记录它的存在与参数，不在这里重复解释蒙皮数据流。

## 验证

- `scene-graph`（CTest 用例，`tests/SceneTests.cpp`、CMake target `MyRendererSceneGraphTests`）覆盖：合法父子关系被接受、循环层级被拒绝（`hierarchy cycle should be rejected`）、10 个独立 Entity 的层级世界矩阵传播、`beginFrame()` 建立运动历史并保留上一帧世界矩阵、`duplicateEntity` 产生根层级副本、删除父对象后子对象回到根层级、以及 `createEntityWithId` 之后新 ID 继续递增。
- `scene-draw-list`（CTest 用例）覆盖 `RenderItem` 生成路径。
- `asset-import` 覆盖 Assimp 侧的节点/网格/材质语义；`ModelNodeData` 与 `GpuModel::DrawCommand` 的分层就是为这条测试与 GPU 侧复用 VAO/VBO/EBO 服务的。
- `shader-hot-reload-smoke`（`add_custom_target`，不是 CTest 用例）用真实 OpenGL 上下文验证失败安全的 Shader 热重载，因此热重载路径必须真正编译链接才能通过。
- `foundation-visual-regression` 是 SR-P0 的官方验收口：固定 1920×1080、MAE `0.02`、变化像素 `0.10`，比较三张基线；target 在缺基线时报 `FATAL_ERROR`，比对失败同样非零退出。
- `renderer-regression-suite` 把本套件与 Prism-5、Glass-2C/3/4、Deferred、Local Lights、Instance Stress、Screen Space、Skinning 串起来，末尾汇总失败套件，因此 SR-P0 的任一基线失败会让整套验收失败。
- `regression-baseline-audit.md` 记录本套件在 Kloofendal EXR 成为新固定输入前的一次失败与重锚定后的通过：`sr_p0_scene_entities` 当时报出 MAE `0.120244`、变化 `94.5715%`，接受基线更新后十个套件全部 Pass，阈值没有被放宽。
- 未验证项：`scene-graph` 用例断言的是它自己构造的 10 个独立 Entity（`scene.size() == 10U`，1 个 root + 1 个 child + 8 个 `Shared instance`），这与 `MYRENDERER_SCENE_FOUNDATION_DEMO=1` 的渲染夹具不是同一组对象——后者会创建 12 个 Entity（主对象、对照实例、9 个 `Shared scene instance` 与地面），其中对照实例默认关闭（`showComparisonObject_` 为假），因此画面里是 10 个可见立方体。本文按工作树代码记录这条差异，没有替原文改写「至少 10 个 Entity」的说法。

> **待复核**：`tools/FoundationVisualRegression.cmake` 传给 `sr_p0_scene_entities` 的 `MYRENDERER_GROUND` 是 `0`（该函数把 `MYRENDERER_GROUND` 与 `MYRENDERER_SCENE_FOUNDATION_DEMO` 都接到 `scene_demo` 参数上），而脚本自己的逻辑随后用 `showGroundPlane_ = true` 打开地面，因此地面在基线图里是可见的。这一处不一致按原样保留，本文没有改脚本，也没有据此判断哪一边是意图。

## 限制与取舍

- **Scene 只保存运行时内存状态**，尚无场景文件序列化、Undo/Redo 或 Prefab；`.myscene` 保存的是渲染器设置与 playback 参数，不是 Scene 图。
- **Pass Context 已显式化状态和资源说明，但不负责资源别名、调度或屏障。** 顺序执行器是当前选择，升级 Render Graph 需要先有真实跨队列/跨分辨率需求作证据。
- **Hot Reload 针对现有 Vertex/Fragment Program；尚无 Include 依赖追踪。** 被 `#include` 的公共着色器片段改动后不会自动触发重载，需要改到入口文件本身。
- **骨骼动态 Bounds 是保守球体**，不是每帧精确 Skinned AABB；它牺牲剔除精度换取动画不被错误剔除。
- **OpenGL 3.3 无 Compute/SSBO**；GPU Driven、海洋 FFT 与体积效果需要后续 API 决策，不在本阶段范围内。
- **基线图与本文并非同一提交**：`sr_p0_scene_entities` 与 `sr_p0_object_motion` 最近一次入库是 2026-09-02 的 `d11f4e7`，2026-09-16 的 P0-A 重锚定又在工作树里改写了 `sr_p0_scene_entities` 与 `sr_p0_skin_motion`（`sr_p0_object_motion` 未变），那一轮尚未提交。
- **阈值 MAE `0.02`、变化像素 `0.10` 比其它视觉套件宽**（Prism-5、Glass-2C/3/4 用 `0.015` / `0.08`）；这是本套件在 `tools/FoundationVisualRegression.cmake` 里写死的值，不是可以随场景调整的默认值。
- **`renderer-benchmark-suite` 只保留各专项 JSON 中的硬件、分辨率、质量档位、CPU/GPU P50/P95、Draw Call 与显存估算**，SR-P0 没有专属的性能表，因此本文不给性能数字；需要数字时应引用对应专项阶段文档。

## 复现命令

```powershell
cmake -S . -B build-release -DBUILD_TESTING=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
cmake --build build-release --target foundation-visual-regression
cmake --build build-release --target renderer-regression-suite
cmake --build build-release --target renderer-benchmark-suite
cmake --build build-release --target package
```

完整回归套件会串起现有 Prism、Glass、Deferred、多灯、Instancing、TAA/SSAO、Skinning 和 SR-P0 固定画面。Benchmark 套件保留各专项 JSON 中的硬件、分辨率、质量档位、CPU/GPU P50/P95、Draw Call 与显存估算。

单张重拍示例（场景实体基线）：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'; $env:MYRENDERER_RENDER_WIDTH='1920'; $env:MYRENDERER_RENDER_HEIGHT='1080'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'; $env:MYRENDERER_SCREENSHOT_WARMUP='4'
$env:MYRENDERER_MSAA='1'; $env:MYRENDERER_RENDER_PATH='1'
$env:MYRENDERER_PBR='1'; $env:MYRENDERER_IBL='1'; $env:MYRENDERER_SHADOWS='0'
$env:MYRENDERER_BLOOM='0'; $env:MYRENDERER_GRID='0'; $env:MYRENDERER_AXES='0'
$env:MYRENDERER_SCENE_FOUNDATION_DEMO='1'; $env:MYRENDERER_GROUND='1'
$env:MYRENDERER_SCREENSHOT='build-release/sr_p0_scene_entities.png'
build-release/Release/Iris.exe assets/models/cube.obj
```

## 下一步

SR-P1A 已在此基础上加入只读 `SceneSnapshot`、Ray/AABB、Ray/Triangle、`SurfaceInteraction` 与 Median-Split BVH，让 Rasterizer 与 CPU Reference Path Tracer 复用同一实例、材质、纹理、相机和灯光/环境数据。设计、测试和后续采样阶段见 [`reference-path-tracer.md`](reference-path-tracer.md)。

1. Scene 图序列化仍然没有路线图条目：`.myscene` 目前保存渲染器设置与 playback 参数，若要让 Scene 面板的层级随场景一起存取，需要先按 `todolist.md` 的写法写成带验收口的工作包。
2. 上一帧蒙皮数据与蒙皮动态 Bounds 只做兼容维护，动态 BLAS Refit/Rebuild 留到 GPU RT 专项测量——对应 `todolist.md` 第 5 节 `P2-C：CPU Reference 增量维护`，以及 `P1-B` 的 GPU RT 阶段。
3. 是否升级 Render Graph 取决于 Path Tracer、NPR、水体和体积效果是否真的出现跨队列/跨分辨率需求；在那之前不为假设需求重构现有 Pass Context 与顺序执行器。
