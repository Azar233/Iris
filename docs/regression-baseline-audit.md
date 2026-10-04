# P0-A 回归基线审计（Regression Baseline Audit）

- 审计日期：2026-09-16
- 源码 revision：`35a726c`，加上下文描述的自动化隔离改动
- 构建目录：`build-ci-msvc`，Visual Studio 17 2022，Release，`BUILD_TESTING=ON`
- GPU / 驱动 / OpenGL：NVIDIA GeForce RTX 4060 Laptop GPU / NVIDIA 591.44 / OpenGL 3.3.0

## 目标与范围

这次审计记录 SR-P2C 工作开始之前的回归状态：哪些验收面通过、固定对照的跨算法差异是多少、Raster 固定图为什么漂移、以及 Kloofendal EXR 作为新固定输入的接受决定。

本文档只记录状态与决策，自己不改写任何版本化图片——它不替换也不更新 `docs/images` 与 `docs/reference-images` 中的任何一张图；实际的基线改写由带 `UPDATE_BASELINES=ON` 的一轮脚本执行完成，见「基线更新决定」。

## 验收矩阵

| 验收面 | 结果 | 证据 |
| --- | --- | --- |
| Release 构建 | Pass | `cmake --build build-ci-msvc --config Release --parallel` |
| CTest | Pass | 7/7 用例通过（该 revision 下 `BUILD_TESTING=ON` 注册的全部用例） |
| `gpu-smoke` | Pass | 真实 OpenGL 上下文覆盖导入、蒙皮、PBR、透明、恢复与演示路径 |
| `path-tracing-regression` | Pass | Beauty 加七组 AOV 的 HDR/PNG 对照全部报告相对 RMSE `0` |
| `path-tracing-raster-comparison` | Pass | 三个固定 `.myscene` 套件都产出了完整产物 |
| `stylized-acceptance` | Pass | PBR/Toon、Forward/Deferred、分辨率、TAA、描边与透射边界检查通过 |
| `renderer-regression-suite` | 接受基线更新后 Pass | 10/10 视觉套件通过；独立复核采集对每张图报告 MAE `0`、变化像素 `0%` |

固定的 Raster / Path Traced 对照报告如下：

| 场景 | MAE | RMSE | 变化像素 |
| --- | ---: | ---: | ---: |
| `10_reference_pathtracer_pbr_hdri` | 0.095589 | 0.132912 | 58.578491% |
| `11_reference_pathtracer_lights` | 0.068002 | 0.124010 | 65.144348% |
| `12_reference_pathtracer_volume` | 0.134296 | 0.189923 | 76.930237% |

这些是诊断性的跨算法差异，不是零误差对齐阈值；该验收目标通过的是它的产物与元数据合同。

在接受基线更新之前，把编辑器选择描边从自动化采集里去掉，暴露出下面这组历史图不一致：

| 视觉套件 | 结果 | 代表性指标 |
| --- | --- | --- |
| Prism-5 | Pass | `prism5_continuous_21`：MAE 0.004129，变化 5.67776% |
| Glass-2C | Fail | `glass2c_msaa1`：MAE 0.154608，变化 98.5024% |
| Glass-3 | Pass | 调试视图回到 MAE 0；最终视图仍在阈值内 |
| Glass-4 | Fail | `glass4_volume_final`：MAE 0.154601，变化 98.4996% |
| Deferred | Fail | `gp_p1_forward_final`：MAE 0.109568，变化 83.1615% |
| Local lights | Fail | `gp_p1b_forward_lights8`：MAE 0.0103143，变化 9.59833% |
| Instance stress | Fail | `gp_p1c_optimized`：MAE 0.00654899，变化 8.40205% |
| Screen space | Fail | `gp_p1d_baseline`：MAE 0.109762，变化 89.3447% |
| Skinning | Pass | 关节/权重调试视图回到 MAE 0 |
| Scene foundation | Fail | `sr_p0_scene_entities`：MAE 0.120244，变化 94.5715% |

## 漂移诊断

主导的漂移来自固定输入变化，而不是 Glass-2C 算法回归的证据：

1. 历史的 Glass-2C 与 GP-P1 基线是在 Poly Haven `delta_2_2k.hdr` 下采集的；活动的 Glass-2C 基线最后一次更新在 commit `245b663`。
2. Commit `ce77754` 有意移除了那个环境，替换为 `kloofendal_48d_partly_cloudy_puresky_4k.exr`。
3. `EnvironmentMap.cpp` 现在无条件加载 Kloofendal EXR。旧环境已不在工作树里，而历史图没有重拍。
4. 旧自动化还允许编辑器选择描边在实体选择成为编辑器工作流的一部分之后进入模型截图。本次审计让 `MYRENDERER_HIDE_SELECTION_OUTLINE=1` 在整轮运行中持续生效，并把它应用到每一个遗留视觉脚本。这消掉了橙色描边、让若干调试视图回到精确相等，但对环境敏感的那些图仍然漂移。

Kloofendal 替换改变了可见天空像素、Split-Sum IBL、反射与曝光响应。这既解释了非常大的 Skybox 失败，也解释了暗部压力场景里较小的阈值边缘失败。**不得为了让报告好看而放宽图像阈值来掩盖固定输入不一致。**

## 审计带入的自动化改动

- 所有遗留视觉脚本都显式抑制编辑器选择描边。
- `MYRENDERER_HIDE_SELECTION_OUTLINE` 现在只抑制描边渲染；它不再依赖「在异步模型导入完成前清空选择」。
- `renderer-regression-suite` 现在跑完每一个视觉套件，并在末尾报告完整的失败套件清单，而不是在 Glass-2C 就停下。

## 基线更新决定

Kloofendal EXR 被明确接受为新的固定 Raster 环境。十支既有视觉回归脚本以 `UPDATE_BASELINES=ON` 运行；56 张 PNG 发生变化，内容未变的采集保持原样。这次更新覆盖 Prism、Glass-2C/3/4、Deferred、Local Lights、Instance Stress、Screen Space、Skinning 与 Scene Foundation。`docs/reference-images` 下的 CPU Path Tracer 历史证据没有被修改。

随后一次独立的 `renderer-regression-suite` 常规运行重新采集了每一张图，十个套件全部通过；在参考机器上，每次比较都报告 MAE `0` 与变化像素 `0%`。旧的 Delta 2 图像仍可从 Git 历史找回；`delta_2_2k.hdr` 仍不在活动资产清单里。

## 2026-09-20：级联阴影基线更新（P1-A 切片 3）

第二次被接受的基线变更。与 Kloofendal 那次不同，这次**不是环境或固定输入变化**，而是渲染算法本身改进后阴影质量提升。

### 触发原因

方向光阴影从单个正交盒换成 3 级级联（`RendererSettings::shadowCascadeCount` 默认 3、`shadowCascadeSplitLambda` 默认 0.75）。级联把相机视锥沿视深切分，为每一片单独拟合光源空间盒，因此阴影 texel 更细、覆盖更远。

### 量化差异（Glass-3 固定水晶场景实测）

| | 旧单框 | 级联第 0 级 |
| --- | --- | --- |
| 阴影 texel 尺寸 | 约 `0.0221` 世界单位 | 约 `0.00628` 世界单位（细 `3.5×`） |
| 覆盖半径 | ±4（宽 8） | 约 ±10.3（宽 20.6，宽 `2.6×`） |

### 触发更新的漂移

级联接上后 `renderer-regression-suite` 只有两项超出阈值，其余全部通过：

| 基线 | MAE | changed | 阈值 |
| --- | ---: | ---: | --- |
| `glass3_lightspace_msaa1` | 0.00222 | 10.1% | 0.015 / 8% |
| `glass4_volume_glass_off` | 0.00603 | 8.16% | 0.015 / 8% |

两项都是 MAE 远低于阈值、变化面积略超阈值，与「阴影覆盖面积扩大后其边界扫过更多像素」一致。

### 审查结论

逐张对比新旧图后确认：变化只出现在阴影及其内部的焦散读数上，**没有级联接缝、没有几何或折射变化**。因此接受这次更新，而不是放宽阈值——两个阈值都没有被改动。

### 更新范围

| 套件 | 更新张数 | 入口 |
| --- | ---: | --- |
| Glass-3 | 6 | `-DUPDATE_BASELINES=ON -P tools/Glass3VisualRegression.cmake` |
| Glass-4 | 14 | `-DUPDATE_BASELINES=ON -P tools/Glass4VisualRegression.cmake` |

共 20 张。其余套件（Prism、Glass-2B/2C、Deferred、Local Lights、Instance Stress、Screen Space、Skinning、Scene Foundation）未受影响，未重拍。

更新后 `renderer-regression-suite` 全部套件通过。实现、两道验证关口与「类型改动必须与声明同步」的教训见 [`shadow-cascades.md`](shadow-cascades.md)。

## 命令审计

`README.md`、`docs/reference-path-tracer.md` 与路线图引用的 target 名称都存在于 `CMakeLists.txt`。P0-A 的规范 MSVC 命令是：

```powershell
ctest --test-dir build-ci-msvc -C Release --output-on-failure
cmake --build build-ci-msvc --config Release --target path-tracing-regression
cmake --build build-ci-msvc --config Release --target path-tracing-raster-comparison
cmake --build build-ci-msvc --config Release --target stylized-acceptance
cmake --build build-ci-msvc --config Release --target renderer-regression-suite
```

## 截图

**本文不配图，这是有意的。** 审计的证据类型是验收矩阵、逐套件的差异数字和可执行命令，它们的可核验形式就是本文上面的表格与代码块。被审计的对象本身是 `docs/images/` 与 `docs/reference-images/` 里的版本化图片：把它们内嵌进审计文档既不增加信息量，也会让「这张图是审计证据」与「这张图是被比对的基线」混为一谈，而后者只能在明确批准的基线更新中被改写。需要看图时按下节命令重新采集，产物写入构建目录。

## 验证

本文的验证即上文的验收矩阵、跨算法对照与历史图不一致三张表，加上下面的命令。视觉回归脚本在采集或比对失败时报出 `FATAL_ERROR`，因此这些 target 失败即非零退出：

```powershell
ctest --test-dir build-ci-msvc -C Release --output-on-failure
cmake --build build-ci-msvc --config Release --target path-tracing-regression
cmake --build build-ci-msvc --config Release --target path-tracing-raster-comparison
cmake --build build-ci-msvc --config Release --target stylized-acceptance
cmake --build build-ci-msvc --config Release --target renderer-regression-suite
```

已知失败与未验证项都留在表里，没有被删掉：接受基线更新之前，Glass-2C、Glass-4、Deferred、Local lights、Instance stress、Screen space 与 Scene foundation 七个套件是 Fail；接受 Kloofendal 作为新固定输入并更新基线之后，十个套件全部 Pass。跨算法对照的三组 MAE/RMSE/变化像素是诊断差异，没有设成零误差门槛。

## 限制与取舍

- 漂移的主因是固定输入替换（`delta_2_2k.hdr` → Kloofendal EXR），不是 Glass-2C 的算法回归；图像阈值不得被放宽来掩盖固定输入不一致。
- 接受基线更新的代价是：历史 Raster 图不再与当前工作树的输入对应，旧 Delta 2 图像只能从 Git 历史找回，`delta_2_2k.hdr` 也不在活动资产清单里。
- 所有数字都来自同一台参考机（NVIDIA GeForce RTX 4060 Laptop GPU、驱动 591.44、OpenGL 3.3.0）；本文没有跨机器证据。
- CTest 的 7/7 只对 revision `35a726c` 成立：工作树后来在 `CMakeLists.txt` 的 `BUILD_TESTING` 块里新增了用例，当前注册的用例数已经多于 7，因此这一行不能被读成「当前用例数」。
- 本文不携带插图，原因见「截图」；审计过程没有产出新的版本化图片，也没有改写任何既有基线图（改写由被审计的那轮 `UPDATE_BASELINES=ON` 执行完成）。

## 复现命令

```powershell
# P0-A 的规范入口
ctest --test-dir build-ci-msvc -C Release --output-on-failure
cmake --build build-ci-msvc --config Release --target path-tracing-regression
cmake --build build-ci-msvc --config Release --target path-tracing-raster-comparison
cmake --build build-ci-msvc --config Release --target stylized-acceptance
cmake --build build-ci-msvc --config Release --target renderer-regression-suite
```

基线更新用的等价单支命令（以 Prism-5 为例，其余九支脚本同理；`tools/*VisualRegression.cmake` 读取 `UPDATE_BASELINES`，脚本内部用 `file(COPY_FILE ... ONLY_IF_DIFFERENT)` 写回 `docs/images/`，所以内容未变的采集不会被改写）：

```powershell
cmake -DRENDERER=build-ci-msvc/Release/Iris.exe `
      -DCOMPARATOR=build-ci-msvc/Release/MyRendererImageComparison.exe `
      -DSOURCE_DIR=. `
      -DOUTPUT_DIR=build-ci-msvc/prism5-visual-current `
      -DUPDATE_BASELINES=ON `
      -P tools/Prism5VisualRegression.cmake
```

单套件重拍/比对也可以直接用对应 target，例如 `cmake --build build-ci-msvc --config Release --target prism5-visual-regression`。

## 下一步

- 接受基线更新后的 56 张 PNG、`docs/images/README.md` 的清单条目、本文档以及同期两篇阶段文档目前仍是工作树里的未提交改动（`git status` 中为已修改或未跟踪）。下一步是把它们作为一个明确批准的基线更新整体提交，不要混进后续功能改动。
- `todolist.md` 的 `P0-A：先锁定回归基线` 一节四条全部勾选，完成门槛「现有 Raster 固定图与逐位 Path Tracer 回归无未解释漂移」已满足；后续阶段（`P0-B`、`P0-D`、`P1-0`）新增固定图时应沿用同一审计路径，并保持 `docs/reference-images` 的历史证据不被改写。
- 旧 Delta 2 历史图若要重拍，必须先恢复 `delta_2_2k.hdr` 资产；在它回到活动资产清单之前，这些图只能从 Git 历史读取。
