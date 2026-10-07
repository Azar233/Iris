# Iris 首页截图

## M2-D 原生云海展示

`m2d-hero.png` 为 M2-D 原生阶段的真实 Iris Raster 输出，固定 `1280×720 / 第 0 帧 / 24 FPS / Seed 20261006 / exposure 0.9`。`m2d-native-reel.gif` 包含第 0～23 帧，播放放慢并停留主构图；不是实时帧率证据。构建为 `build-ci-msvc` Release，GPU 为 RTX 4060 Laptop / NVIDIA 591.44 / OpenGL 3.3.0。

原生实现参考 Himalayas 的云形与照明方法，没有复制第三方 GLSL。独立 Enscape study 使用自己的四 Pass 路径及 CC BY-NC-SA 3.0 署名，不与该原生展示混淆。CPU PT 尚未完整支持原生海面。

```powershell
cmake --build build-ci-msvc --config Release --target m2d-hero-acceptance
powershell -NoProfile -ExecutionPolicy Bypass -File tools/M2BSceneJobAcceptance.ps1 -Stage m2d
python tools/M2DHeroPresentation.py
```

详细图像、原性能预算和限制见本地 `docs/m2d-native-hero-release.md`；原始数据在 `build-ci-msvc/m2d-hero` 与 `m2d-scene-job`。

## 原编辑器与 M1 展示

两张图片来自 revision `0d74ca4` 的真实 Iris 编辑器，使用 `build-ci-msvc` Release、NVIDIA GeForce RTX 4060 Laptop GPU、OpenGL 3.3 与驱动 591.44，于 2026-10-04 捕获。窗口尺寸为 1600×900。

- `iris-editor-scene-1600x900.png`：Hierarchy 的 9 个实体、Scene 室内模型、Object Inspector 与 Project 资源分类。
- `iris-editor-1600x900.png`：程序化海面、天空与黄色方块，Renderer Inspector 的材质、PBR 与光照参数。程序化对象不在实体层级中显示。

在项目根目录重拍：

```powershell
cmake -E env MYRENDERER_SMOKE_TEST=1 MYRENDERER_EDITOR_WINDOW_WIDTH=1600 MYRENDERER_EDITOR_WINDOW_HEIGHT=900 MYRENDERER_EDITOR_SCREENSHOT_WARMUP=2 MYRENDERER_EDITOR_SCREENSHOT_TAB=object MYRENDERER_EDITOR_SCREENSHOT=assets/readme/iris-editor-scene-1600x900.png build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/13_polyhaven_studio_lounge.myscene

cmake -E env MYRENDERER_SMOKE_TEST=1 MYRENDERER_EDITOR_WINDOW_WIDTH=1600 MYRENDERER_EDITOR_WINDOW_HEIGHT=900 MYRENDERER_EDITOR_SCREENSHOT_WARMUP=2 MYRENDERER_EDITOR_SCREENSHOT_TAB=renderer MYRENDERER_EDITOR_SCREENSHOT=assets/readme/iris-editor-1600x900.png build-ci-msvc/Release/Iris.exe assets/scenes/03_enscape_ocean_study.myscene
```

截图中的 FPS 仅为当时界面状态，不作为性能结论。

## 2026-10-06 M1 发布代表图

基准为 `c409e8f` 加 M1-B 工作区 HDR/光空间/捕获修复，MSVC Release，RTX 4060 Laptop / NVIDIA 591.44 / OpenGL 3.3.0。下列是说明图副本，不参与固定图比较，不覆盖原历史图。

- `m1-release-glass.png`：`glass3-visual-regression` 的 `glass3_lightspace_msaa4.png`，1920×1080 / 4× MSAA；展示已接受的独立光空间焦散与透射阴影。On/Off、Projector 和 Debug 图位于该目标输出目录，原阈值 MAE 0.015 / changed fraction 0.08 不变。
- `m1-release-prism.png`：`prism5-visual-regression` 的 `prism5_continuous_21.png`，1920×1080 / 4× MSAA；展示 21 个连续波长采样。关闭色散、七色与角度变化保留为同一回归套件的对照，代表图不是性能基准。
- `m1-release-reference.png`：M1-B 的 Volume Raster / PT / Difference triptych，来自 `path-tracing-raster-comparison`。256×256、512 SPP、Depth 8、Seed 20260915、ACES+sRGB / 5×5 Median；它验证产物完整性并记录跨算法差异，不要求零误差。

复现与复制：

```powershell
cmake --build build-ci-msvc --config Release --target glass3-visual-regression
cmake --build build-ci-msvc --config Release --target prism5-visual-regression
cmake --build build-ci-msvc --config Release --target path-tracing-raster-comparison
Copy-Item build-ci-msvc/glass3-visual-current/glass3_lightspace_msaa4.png assets/readme/m1-release-glass.png
Copy-Item build-ci-msvc/prism5-visual-current/prism5_continuous_21.png assets/readme/m1-release-prism.png
Copy-Item build-ci-msvc/path-tracing-raster-comparison/12_reference_pathtracer_volume/triptych.png assets/readme/m1-release-reference.png
```

原生云海最终视觉收口属于 M2。CPU PT 水面/空中透视缺失、焦散近似与跨硬件差异继续保留在阶段记录中；不以本组代表图宣称全部后端等价。

## GLSL 云海展示（2026-10-07）

`glsl-ocean-hero.png` 是 01 场景的真实 Iris 输出，1280×720、time 1.25 s、原 03 的 GLSL 波浪/光照参数，默认隐藏方块。对应展示图与 GLSL 按 CC BY-NC-SA 3.0 署名 Thomas / @Thomas_ensc（Enscape Cube）、Alexander Alekseev / TDM（Seascape），许可见 `shaders/third_party/enscape_cube/LICENSE.md`。原 `m2d-hero.png` 与 GIF 保留为原生阶段历史证据，当前原生入口位于 fixtures/28_native_ocean_clouds.myscene。

同日更新为开启 `enscapeNoiseReduction` 的输出，来自 `build-ci-msvc/ocean-realism/a803732c/hero.png`。复现运行 `tools/GlslOceanAcceptance.ps1`；当前近景对照、滤波代价及性能记录见本地 `docs/m2-glsl-ocean-noise.md`。
