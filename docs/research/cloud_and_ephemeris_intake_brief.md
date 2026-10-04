# 体积云工程实践与天文星历接入调研简报（Iris）

> 文档性质：调研输入（research brief），**不是**阶段实施记录，因此不含截图与实测数字；其中的成本与收益均为公开资料的量级估计，落地时必须在本机重新标定。它是 [`volumetric_clouds_brief.md`](volumetric_clouds_brief.md) 的**增量补充**，不替换该简报的主线结论。基准证据请见各阶段文档。

本文回答两个具体问题：

1. 两篇中文教学文章 [RayMarching实时体积云渲染入门(上)](https://zhuanlan.zhihu.com/p/248406797) / [(下)](https://zhuanlan.zhihu.com/p/248965902)（作者 ShaderFallback，Unity URP + [UnityVolumeCloud](https://github.com/ShaderFallback/UnityVolumeCloud)）中，有哪些工程细节值得本项目的切片 6 吸收。
2. [Astronomy Engine](https://github.com/cosinekitty/astronomy) 能否作为 `todolist.md` 中"经纬度/日期驱动的日月位置、月相"的参考实现。

> 取证说明：知乎对自动抓取返回 HTTP 403，本文所引正文取自 Wayback Machine 存档（[上篇](https://web.archive.org/web/2022/https://zhuanlan.zhihu.com/p/248406797)、[下篇](https://web.archive.org/web/2022/https://zhuanlan.zhihu.com/p/248965902)）；Astronomy Engine 的能力、API 签名与许可以 `source/c/astronomy.h`、`LICENSE` 与 GitHub API 的实测返回为准，不依据其 README 的宣传口径。

---

## 1. 结论摘要

两篇文章与 Astronomy Engine 分别落在本项目的两条不同主线上，**没有冲突**：

| 来源 | 归属 | 结论 |
| --- | --- | --- |
| 两篇体积云文章 | P1-A 切片 6 的**质量档与工程细节** | 有 3 项可直接吸收、1 项借鉴、1 项不采纳 |
| Astronomy Engine | P1-A 新增切片 7（`todolist.md` 唯一标注"未实现"的天文项） | 可接入；但**不含星表**，恒星部分需自备数据 |

两条线**互相独立**：切片 6 改渲染，切片 7 改模块与参数来源。但它们都触碰 `RendererSettings::atmosphere` 与 `EditorDomain`，因此建议**串行**而非并行，理由见 §4。

---

## 2. 两篇体积云文章：可借鉴项与明确不采纳项

### 2.1 两者的定位差异（先说清楚，避免误用）

| | 两篇文章 | 本项目切片 6 |
| --- | --- | --- |
| 云的容器 | 一个 **Box 包围盒**（`boundsMin/Max` = Transform 的 position ± scale/2），靠 `edgeWeight` 衰减四周硬边 | **球壳 slab**（云底 1.5–3 km、云顶 4–8 km） |
| 光照 | Beer-Lambert 透射 + 8 步 light march；**内散射积分缺失**，云色由"亮→灯光色 / 中→ColorA / 暗→ColorB"三段艺术插值给出，原文自述"这一点都不物理" | 单次散射积分 + 双叶 HG + Hillaire 多重散射近似，颜色由解析大气模型给出 |
| 相位函数 | 双叶 HG，`g` 为自由调参量 | 双叶 HG，前向 `g≈0.8` / 后向 `g≈-0.3` / 混合 `0.5` |
| 太阳一致性 | 仅 `_WorldSpaceLightPos0`，无天空模型 | 与 shadow map、`sunTransmittance()`、CPU Path Tracer **共用同一 `sunDirection()`** |
| 时间维度 | 分帧 / 棋盘格**空间分割**更新 | 时间重投影 + 独立 volume motion vector + history 拒绝 |
| 确定性 | 抖动取自 `_Time.y`，无复现合同 | 一等公民 `determinism` 开关、可哈希离线资产、预热帧数写入元数据 |

因此**不能**把文章的光照与几何直接搬过来——那会把本项目已经建立的物理层级与可复现合同降级。可借鉴的是它的**工程细节**。

### 2.2 采纳项 A：深度图降采样的取极值规则（→ C4）

文章在讲 1/4 降采样时给出了一条明确规则：**深度图存的是距离，不能像颜色那样做 4 像素平均，应取 min 或 max**，并给出 4-tap 代码（`min(min(d1,d2), min(d3,d4))`）。

理由与项目现有实现一致但项目缺这一步：项目只有 opaque depth，云又位于远景，若用均值下采样，物体与云的边界深度会被抹平，深度引导升采样会在轮廓处出现阶梯。

**安排**：写入 C4 的实现要求。取 `min` 还是 `max` 由"用低分辨率 pass 自身首个非零密度处深度"这一既有决策决定，不照抄文章的 opaque-depth 语义。

### 2.3 采纳项 B：边缘重绘（→ C4）

文章引用 GPU Gems 的**边缘重绘（edge redraw）**：先用深度图定位几何边缘，然后在绘制云时让边缘像素改采**全分辨率**深度。

项目简报 §4 只提了"用 transmittance 的 alpha 加权代替纯深度双线性"。边缘重绘是更直接的补充手段，两者不冲突：alpha 加权解决天空像素 `depth=far` 的混合问题，边缘重绘解决几何轮廓的阶梯问题。

**安排**：写入 C4 的可选增强，并在 C4 验收里要求出图对比"仅 alpha 加权"与"alpha 加权 + 边缘重绘"，作为已知 artifact 的量化记录。

### 2.4 采纳项 C：两次采样的廉价光照（→ C3 的 Low 档 / 回退档）

下篇的性能优化第 3 条给出：光照可以对密度只采 2 次——一次原位，一次沿光方向偏移（原文偏移 `20` 世界单位）——用两次密度的差异模拟暗部，替代完整 light march。

**安排**：作为 C3 的**独立质量档**，不替换主路径。原因是它是**有偏近似**：本项目第 8.2 节的完成定义与 CPU 参考 raymarch 交叉验证要求主路径可被数值验证，一个两采样近似无法通过该验证。因此：

- 主路径（High）= 完整 light march + 多重散射近似，接受 CPU 参考验证；
- 回退档（Low / 低配）= 两采样近似，**在文档与 UI 中明确标注为有偏、不参与 RMSE 验收，也不得用于基线入库**。

这条安排与项目已有先例一致：P0-D 的 Firefly Clamp 同样是"可选有偏模式，不用于让指标好看"。

### 2.5 借鉴项 D：分帧 / 棋盘格更新（→ P2，不进切片 6 主路径）

下篇的"分帧渲染"是**空间分割**：把屏幕分成 4 块 RT 分摊到 4 帧，或棋盘格分割 + MotionVector 推测上帧位置。它本质是省算力，**不是降噪**，原文也承认均分屏幕在高速移动时容易穿帮。

本项目的主路径已经是时间重投影。若同时引入棋盘格更新，就会出现**两套累积机制**争夺同一 history，违反"避免出现第二套同类管线"的项目取向（该取向已写在 `todolist.md` P2-A 关于体积/几何 TAA 缓冲的条目里）。

**安排**：登记为 P2 备选，**进入条件**是"时间累积在低配档不可用或成本超标"；在此之前不为假设需求实现棋盘格路径。不写入切片 6。

### 2.6 文档方法借鉴 E：逐阶段 before/after 出图（→ 切片 6 的文档义务）

两篇文章最有价值的其实不是代码，而是**叙事结构**：每一步（采天气图 → 加 height gradient → 加 edge fade → 再映射软底 → 乘主体噪声 → 细节侵蚀）都给一张前后对照图，让读者看到每个参数改变了什么。

本项目的 `docs/README.md` 第 3 节已经要求"前后对照合成一张图优于贴两张独立图"，但**尚未要求按实现步骤分段出图**。

**安排**：切片 6 的 C1～C5 每完成一个工作包，在阶段文档里保留一张该步骤的 before/after（写入 `docs/media/`，不参与任何自动比对）。这样最终文档能解释"密度重映射与侵蚀各自贡献了什么"，而不只是展示一张成品图。

### 2.7 明确不采纳项

| 项 | 决策 | 原因 |
| --- | --- | --- |
| 用 Box 包围盒承载云层 | 不采纳 | 球壳 slab 才是真实大气层结构；方盒在抬升视角会暴露顶底面，只能靠 edge fade 掩盖 |
| 三段颜色映射代替散射积分 | 不采纳 | 有偏且不可验证，会使本项目的 CPU 参考 raymarch 交叉验证失去意义 |
| 抖动取自 `_Time.y`、光照依赖 wall-clock | 不采纳 | 直接破坏确定性合同；项目必须用帧号推导时间 |
| 依赖预生成 3D 贴图资产但不记录来源 | 部分不采纳 | 噪声资产必须由 CPU 生成且可哈希校验（项目已有结论），若外部导入则需记录生成参数与许可证 |

---

## 3. Astronomy Engine：依赖评估与接入安排

### 3.1 依赖事实（实测）

| 项 | 值 |
| --- | --- |
| 许可 | MIT，`Copyright (c) 2019-2025 Don Cross <cosinekitty@gmail.com>` |
| C 源码 | `source/c/astronomy.h`（64 KB）+ `astronomy.c`（481 KB），两个文件，无子模块 |
| 外部依赖 | **零**。`#include` 仅 `math.h / stdint.h / stdio.h / stdlib.h / string.h / time.h / sys/time.h / windows.h` |
| C++ 绑定 | **无** `source/cpp/`；仅 C / C# / JS / Kotlin / Python。C 头文件可由 C++17 直接包含 |
| 模型与精度 | VSOP87（行星/太阳）+ NOVAS C 3.1（月球），声称 ±1 角分，对 JPL Horizons 做过单元测试 |
| 版本固定 | 使用带 tag 的 release（实测最新 `v2.1.19`），与现有依赖的 `GIT_TAG` 模式一致 |
| 星表 | **不含**。仅有 `BODY_STAR1..8` 与 `Astronomy_DefineStar`（最多 8 颗自定义星） |

引入方式与现有依赖同构：`CMakeLists.txt` 已是 `FetchContent_Declare(... GIT_TAG ...)` 固定版本模式（glfw 3.4、glm 1.0.3、glad v2.0.8、tinyobjloader v1.0.6、tinyexr v1.0.13、imgui v1.92.7-docking、assimp v6.0.5）。

### 3.2 坐标约定天然对齐（接入成本低的主要原因）

- Astronomy Engine 的 `AstroHorizon`：`azimuth` 0=北、90=东；`altitude` 为地平线以上角度。
- 本项目的 `Atmosphere.h` 注明方位角"自 `+Z` 向 `+X` 度量"，且 `sunDirection()` 实现为 `x = cos(el)·sin(az), y = sin(el), z = cos(el)·cos(az)`，同时 `scatteringRadiance()` 以 `direction.y` 为天顶。

即项目世界系里 **+Y = 天顶、+Z = 北、+X = 东**，与 HOR 水平系逐轴一致：

```
renderer.atmosphere.sunElevationDegrees = horizon.altitude;
renderer.atmosphere.sunAzimuthDegrees   = horizon.azimuth;
```

**不需要坐标系旋转、手性修正或角度换算。** 这是本项接入最大的成本优势。

### 3.3 API 对应表

| 需求 | API |
| --- | --- |
| 太阳高度角/方位角 | `Astronomy_Equator(BODY_SUN, ...)` → `Astronomy_Horizon(time, observer, ra, dec, refraction)` |
| 月球高度角/方位角 | `Astronomy_Equator(BODY_MOON, ...)` → 同一个 `Astronomy_Horizon(...)` |
| 月相 / 照明比例 | `Astronomy_Illumination(BODY_MOON, time)`（当前月盘恒为满盘） |
| 朔望与月相角时刻 | `Astronomy_MoonPhase`、`Astronomy_SearchMoonPhase`、`Astronomy_NextMoonQuarter` |
| 日出日落 | `Astronomy_SearchRiseSetEx(body, observer, DIRECTION_RISE, ..., metersAboveGround)` |
| 民用/航海/天文暮光 | `Astronomy_SearchAltitude(..., altitude)` 指定任意高度角 |
| 坐标变换 | `Astronomy_Rotation_*`（EQJ / EQD / ECL / ECT / HOR / GAL 互转） |
| 大气折射修正 | `Astronomy_Refraction` / `Astronomy_InverseRefraction`，`Horizon` 可带折射 |

### 3.4 接入前必须满足的四条硬约束

这四条是本项目特有纪律，直接决定"能不能用"而非"好不好用"：

1. **禁止 `Astronomy_CurrentTime()`。** 渲染序列不得依赖 wall-clock，时间必须由「帧号 ÷ 固定 FPS」从 `Timeline` 推导，经 `Astronomy_MakeTime()` / `Astronomy_TimeFromUtc()` 送入。
2. **`parametersMatch()` 缓存键必须扩容。** 该函数是 Raster 环境立方体重建与 CPU Path Tracer 环境捕获共用的缓存闸门（容差：角度 `0.35°`、参数 `0.01`）。太阳位置一旦由「星历 + 经纬度 + 时间」导出，**日期/UTC/经纬度必须一并进入比较**，否则经纬度变化而角度恰好落在容差内时会复用陈旧环境。
3. **`SceneContentHash` / Simulation Cache 必须跟进。** 新增的经纬度与历元字段需进入场景内容哈希，否则 Simulation Cache 会在经纬度变化时报 `Hit` 而复用错误帧——本项目对该情形已有 `Hit` / `Missing` / `Stale` 的明确分类纪律。
4. **`.myscene` 必须向后兼容。** 按 `todolist.md` 第 8.2 节第 4 条，新增可选字段（`ephemerisEnabled` / `latitude` / `longitude` / `utcStart` 等），默认关闭时仍走现有手工轨迹；否则既有 13 帧海岸序列基线与 `coastal-sequence-acceptance` 的逐帧哈希会全部漂移。

另需在接入时确认 `astronomy.c` 的全局状态（`Astronomy_Reset`、`Astronomy_SetDeltaTFunction`）在项目线程模型下的行为。风险低——日月位置只在 `applyPresentation` 求值一次——但必须在接入前确认，不能假设。

### 3.5 架构收益

项目已把太阳做成**单一标量源**：`sunElevationDegrees` / `sunAzimuthDegrees` 同时驱动解析天空、方向光颜色与能量、CSM 阴影、水面高光，以及 CPU Path Tracer 的 `captureSceneLighting()`（`src/pathtracer/SceneLighting.cpp` 的月光方向同样取自 `atmosphere::moonDirection()`）。

因此**在 `applyPresentation` 里替换这一个标量，Raster 与 CPU Path Tracer 会同时变正确**，无需改动渲染器、Shader 或 Path Tracer。这正是"同一个太阳"设计当初的目标，也是本项性价比高的根本原因。

---

## 4. 排序与依赖

### 4.1 为什么建议串行

切片 6 与切片 7 都修改 `RendererSettings::atmosphere` 与 `EditorDomain` 的领域快照/命令映射，并行会产生合并冲突并使 `editor-session` 的领域载荷覆盖（现为 27 条命令）反复返工。

### 4.2 边界约束（使串行成本可接受）

切片 7 的落地形态应限定为**新增独立文件**，不修改 `Atmosphere` 现有函数签名：

- 新增 `src/astronomy/SolarEphemeris.*`（查询与角度导出）；
- `Atmosphere` 仍只接收 `sunElevationDegrees` / `sunAzimuthDegrees` 等标量，**保持单向依赖**，由 `Application` / 模块层负责把星历结果写入参数。

这样切片 7 对既有渲染路径几乎零侵入，试错与回退成本都低。

### 4.3 顺序

1. **切片 6 体积云**（唯一待启动主线，不变）
2. **切片 7 日月星历**（本简报 §3，规模小、独立、不阻塞）
3. **切片 8 恒星与星座**（可选，需先决定星表数据来源）
4. 之后进入 **P1-B Vulkan / GPU Path Tracing**

切片 7 虽小，但建议**不要推迟到 P1-B 之后**：一旦 GPU Path Tracing 启动，跨后端（Raster / CPU PT / GPU PT）的日月一致性就要在三条路径上同时验证，成本显著高于现在两条路径时接入。

---

## 5. 分阶段工作包与验收口

### 切片 7-A：太阳与月球位置（必做）

- `CMakeLists.txt` 增加 `FetchContent_Declare(astronomy GIT_REPOSITORY https://github.com/cosinekitty/astronomy.git GIT_TAG v2.1.19)`，并加入 `FetchContent_MakeAvailable`；同步 `THIRD_PARTY_NOTICES.md`、`ASSET_LICENSES.md`（如涉及资产）与 `README.md` 的依赖与构建说明。
- 新增 `src/astronomy/SolarEphemeris.*`：输入（纬度、经度、海拔、UTC、是否折射修正），输出太阳与月球的 `{elevation, azimuth}`。编译为 C++17，对 C 头文件使用 `extern "C"`。
- `.myscene` 新增可选字段，默认关闭；`ParameterRegistry` 暴露经纬度与日期，Inspector 从同一元数据生成控件。
- 新增 `astronomy-model` 测试目标（纯 CPU、无 OpenGL，可在 CTest 直接跑）。

**验收**：① 关闭星历时既有场景行为逐位不变；② 给定已知地点与时刻，太阳/月球高度角与方位角落在公开星历的容差内；③ 同一帧号重复求值结果完全一致；④ `coastal-sequence-acceptance` 的既有 13 帧哈希在星历关闭时不变。

### 切片 7-B：月相（必做，纯增益）

- 用 `Astronomy_Illumination` 驱动月盘亮度与形状，替换当前恒定满盘。
- 月相为纯视觉增益，不改变任何既有几何或 BSDF 合同。

**验收**：朔/望/上下弦四组固定机位的月盘出图；月相变化正确影响夜间水面照明强度；月盘形状不引入新的时序不稳定性。

### 切片 7-C：日出日落与暮光（可选）

- 用 `SearchRiseSetEx` / `SearchAltitude` 校正"夜晚判定"，替换当前基于仰角 `+10° → −8°` 的硬编码淡出。
- 该项会**改变现有昼夜序列的画面**（暮光过渡时刻变化），因此必须在切片 6 与 7-A/7-B 的基线固化之后独立进行，并独立审查基线漂移。

### 切片 8：恒星与星座（可选，需先决策）

`Astronomy_DefineStar` 只支持 8 颗自定义星，**没有内置星表**。因此本项目"真实星空"缺的是**数据**，不是算法。可选路径：

| 路径 | 说明 | 取舍 |
| --- | --- | --- |
| 保留程序化星点 | 维持现状（方向哈希） | 视觉可用、零数据成本，但星空位置无天文意义 |
| 内置精简亮星表 | 取 Yale BSC / Hipparcos 子集（约 200–9000 颗），随仓库版本化 | 与项目"离线、无网络可复现"取向一致；需处理许可证与体积 |
| 外部星表导入 | 用户提供 CSV，运行时解析 | 不符合"无外部依赖即可复现"的默认体验 |

进入条件：先决定数据来源与许可证，再立项。**在此之前保持程序化星点，不假装天文准确。**

---

## 6. 风险

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 星历引入新的失效维度 | 相机不变而时间推进时环境缓存可能被误复用 | §3.4 第 2、3 条作为**接入前置条件**，非事后修补 |
| 切片 7 改动既有昼夜基线 | 13 帧海岸序列哈希漂移 | 默认关闭 + 7-C 独立成切片 + 独立审查基线变更 |
| 全局状态与线程模型冲突 | 后台渲染线程读到半更新状态 | 接入前确认；位置只在 `applyPresentation` 单点求值 |
| 恒星数据来源未定 | 切片 8 无限期搁置 | 明确列为可选并给出三条路径，不阻塞主线 |
| 文章有偏技巧被误用于主路径 | 破坏 CPU 参考验证与确定性合同 | §2.4 已限定为独立 Low 档并禁止参与基线入库 |

---

## 7. 参考链接

- 体积云教学（Unity）：[上篇](https://zhuanlan.zhihu.com/p/248406797)、[下篇](https://zhuanlan.zhihu.com/p/248965902)、工程 [ShaderFallback/UnityVolumeCloud](https://github.com/ShaderFallback/UnityVolumeCloud)
- 该教案的上游来源（与本项目主线一致）：Andrew Schneider, *The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn*, SIGGRAPH 2015；Sébastien Hillaire, *Physically Based Sky, Atmosphere & Cloud Rendering in Frostbite*, SIGGRAPH 2016
- Astronomy Engine：[仓库](https://github.com/cosinekitty/astronomy)、[MIT LICENSE](https://github.com/cosinekitty/astronomy/blob/master/LICENSE)、C API 头文件 `source/c/astronomy.h`

**未核实项声明**：两篇文章的源码未在本机编译验证，其性能数字（GTX 1080 下 18 ms → 降采样 1/4 后 4 ms）为其作者自述，未复现；Astronomy Engine 的 ±1 角分精度为其自述指标，本项目未在本机对 JPL Horizons 做逐点比对，切片 7-A 的验收必须自行完成该比对，不得直接采信。
