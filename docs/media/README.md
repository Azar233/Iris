# 作品集与文档媒体

本目录存放**文档插图**：用来解释功能、UI 或前后对照的截图与视频，不参与任何自动像素比对。规范见 [`../README.md`](../README.md) 第 3 节。

回归基线与历史证据在 [`../images/`](../images/)、[`../reference-images/`](../reference-images/) 与 [`../performance/`](../performance/)，它们的清单在各自的 `README.md`；不要把说明性截图放进那三个目录。

## 现有素材

| 文件 | 内容 | 重拍方式 |
| --- | --- | --- |
| `prism5_demo_reel.mp4` | Prism-5 确定性 360 帧参数动画，24 fps 编码为 15 秒 1280 × 720 作品集预览 | 见 [`../prism5-validation.md`](../prism5-validation.md)；PNG 序列只生成在构建目录，不入库 |
| `p1-workspace-1440x900.png` | 默认 Dock 工作区：中央 Viewport、Scene Explorer、Inspector、底部多标签工作区 | `MYRENDERER_EDITOR_WINDOW_WIDTH=1440 MYRENDERER_EDITOR_WINDOW_HEIGHT=900` + `MYRENDERER_EDITOR_SCREENSHOT=docs/media/p1-workspace-1440x900.png` 运行 `build-ci-msvc/Release/MyRenderer.exe assets/scenes/fixtures/18_atmosphere_sky.myscene` |
| `p1-workspace-hierarchy-1440x900.png` | Scene Explorer 树形层级：一个根节点和四个子节点，统计默认折叠 | `MYRENDERER_SMOKE_TEST=1`、窗口 `1440×900`、`MYRENDERER_EDITOR_SCREENSHOT=docs/media/p1-workspace-hierarchy-1440x900.png`，运行 `build-ci-msvc/Release/MyRenderer.exe assets/scenes/fixtures/01_multi_model_hierarchy.myscene` |
| `p1-workspace-1100x680.png` | 同一工作区在 1100 × 680 应用下限下的布局与可达性 | 同上，窗口尺寸改为 `1100` × `680` |
| `p1-workspace-render-queue.png` | Render Queue 标签页：任务路径输入、Enqueue、空队列状态与恢复诊断 | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=render-queue` |
| `p1-workspace-modules.png` | Modules 标签页：真实 Module Registry 清单（ID / Name / Kind / Target / Source / API 版本 / Build ID） | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=modules` |
| `p1-workspace-log-profile.png` | Log / Profile 标签页的汇总诊断 | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=log` |
| `c1-module-inspector.png` | Inspector 的 Module 页：模块选择、Seed 与空状态文案 | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=module` |
| `p1a-atmosphere-keylight-before-after.png` | 逐通道关键光颜色接入前后的金时刻对照（960 × 540 双栏合成） | `MYRENDERER_SUN_ELEVATION=10 MYRENDERER_SUN_AZIMUTH=120` 下各拍一张 960 × 540 截图后并排合成 |
| `p1a-aerial-perspective-on-off.png` | Aerial Perspective 开关对照：关闭时地面一直铺到地平线，开启后远景失去对比度并向天空色靠拢（960 × 540 双栏合成） | `MYRENDERER_SUN_ELEVATION=14 MYRENDERER_SUN_AZIMUTH=128 MYRENDERER_SKY_TURBIDITY=1.4` 下分别用 `MYRENDERER_AERIAL_PERSPECTIVE=0` 与 `=1`（`MYRENDERER_AERIAL_SCALE_HEIGHT=12`）各拍一张后并排合成 |
| `p1a-shadow-cascade-debug-forward.png` | 海岸夹具的 Forward 三级级联调试图；近、中、远三个色带显示实际选层边界（1280 × 720） | `cmake --build build-ci-msvc --config Release --target shadow-cascade-acceptance` 后复制 `build-ci-msvc/shadow-cascade-acceptance/forward_cascade_3.png` |
| `p1a-water-synthesis-forward.png` | 固定时间的 Gerstner 海面，展示近景波形和远景连续网格（960 × 540） | `cmake --build build-ci-msvc --config Release --target water-synthesis-acceptance` 后复制 `build-ci-msvc/water-synthesis-acceptance/forward_t1.png` |
| `p1a-water-depth.png` | 赭色海床经水深吸收透出，礁石接水处出现岸线泡沫（960 × 540） | 同一验收 target 后复制 `build-ci-msvc/water-synthesis-acceptance/depth_on.png` |
| `p1a-water-underwater.png` | 水下相机的海面与全屏消光（960 × 540） | 同一验收 target 后复制 `build-ci-msvc/water-synthesis-acceptance/underwater_on.png` |
| `p1a-coastal-sequence-noon-to-night.png` | 切片 5 正午、日落、月夜的天空、海况和相机对照（3 × 640 × 360）；夜帧显示月盘、星点与海面月光反射 | `coastal-sequence-acceptance` 输出 `frame_0000/0009/0012.png` 后横向拼接；详见 [`../coastal-sequence.md`](../coastal-sequence.md) |
| `p1a-cloud-layer-on-off.png` | 切片 6 C1 云层开关对照（846 × 506 双栏）：右侧天空被 2D 解析云层覆盖。这张图同时是**该步视觉标定未完成**的证据——云读作均匀薄雾而非有轮廓的云 | `MYRENDERER_SMOKE_TEST=1` 下用 `MYRENDERER_CLOUDS=0` 与 `=1` 各拍一张（输出 `cloud-c1-off.png` / `cloud-c1-on.png`）后横向拼接；详见 [`../cloud-layer-c1.md`](../cloud-layer-c1.md) |
| `p1a-cloud-march-on-off.png` | 切片 6 C2 GPU ray march 的云层开关对照（1700 × 506 双栏）：右侧天空由体积 march 产生，云与晴朗天空几乎等亮，带太阳方向的辐射条纹。形态仍偏细碎，是 C3 及后续要处理的 | `MYRENDERER_SMOKE_TEST=1`、`MYRENDERER_CLOUDS=0` 与 `=1`（`MYRENDERER_CLOUD_COVERAGE=0.45`）各拍一张后横向拼接；详见 [`../cloud-layer-c1.md`](../cloud-layer-c1.md) |

| `p1a-cloud-repetition-before.png` | 周期参数接入后的中间失败状态，二维挤出仍产生规则帘纹（960×540） | 保留构建目录 `ocean-weather-cloud-period-fix.png` 的历史证据；详见 [`../cloud-layer-c1.md`](../cloud-layer-c1.md) |
| `p1a-cloud-repetition-after.png` | 三维 Worley 与确定性像素采样消除重复帘纹，形成独立云团（960×540） | 运行 [`../cloud-layer-c1.md`](../cloud-layer-c1.md) 末节的固定机位截图命令，复制 `build-ci-msvc/cloud-repaired-hero.png` |

| `p1a-cloud-calibrated-presets.png` | 三维云场同机位积云 Low/High 与层云对照；积云形态相同、Low 积分噪声更明显，层云形成连续云盖 | `cloud-layer-acceptance` 输出 `cumulus_low.png`、`cumulus_high.png`、`stratus_high.png`，各缩为 640×360 横向拼接，加 38 像素标题栏；见 [`../cloud-calibration.md`](../cloud-calibration.md) |

| `p1a-cloud-temporal-motion.png` | C4 移动相机的当前云帧、错误屏幕历史混合、独立重投影与错误对照透射率残差；错误对照由测试在 CPU 上构造 | `cloud-temporal-acceptance` 输出四张 `high-moving-*.ppm`，各按 nearest 放大到 384×256 横向拼接并加 38 像素标题栏；见 [`../cloud-temporal.md`](../cloud-temporal.md) |
| `p1a-cloud-temporal-hero.png` | 960×540 海洋 Hero 启用半分辨率与云历史，固定预热 8 帧；近景海面与礁石不进入云历史 | `cloud-temporal-visual-acceptance` 后复制 `build-ci-msvc/cloud-temporal-visual-acceptance/hero.png` |

| `p1a-cloud-shadow-on-off.png` | C6 地面/海面云影开关对照，固定机位下亮度变化较轻，保留环境光 | `cloud-shadow-visual-acceptance` 的地面/海面四张图缩为 640×340，两行拼接，加 40 像素标题栏；见 [`../cloud-shadows.md`](../cloud-shadows.md) |
| `p1a-cloud-shadow-wind.png` | C6 High 档太阳透射率图随风 X 偏移 700 m 移动，白色为 1 | `cloud-shadow-acceptance` 的 `high-base.ppm`、`high-wind.ppm` 按 nearest 放大到 512×512 横向拼接，加 38 像素标题栏 |

| `p1a-god-rays-on-off.png` | C6 实验场景与海洋云隙径向散射开关，观察强度 0.35 的天空亮度变化 | `god-rays-visual-acceptance` 的四张开关图各缩为 640×360，两行拼接并加 40 像素标题栏；见 [`../god-rays.md`](../god-rays.md) |
| `p1a-god-rays-occluder-edge.png` | C6 窄遮挡条内不合成，遮挡条另一侧仍漏光的已知 artifact | `god-rays-acceptance` 的 64×32 双栏 `occluder-edge.ppm` 按 nearest 放大为 1024×512，加 38 像素标题栏 |

| `p1a-cloud-determinism-capture.png` | C7 固定云采样的 0/5 帧预热 PNG 完全相同，与显式时间积累配置对照 | `cloud-determinism-acceptance` 的 `first/warmup/temporal_first` 三个 `frame_0000.png` 按原始 640×360 横向拼接，加 40 像素标题栏；见 [`../cloud-determinism.md`](../cloud-determinism.md) |

| `p1a-cloud-noise-channels.png` | 64³/周期 4 离线噪声的 RGBA 中层切片，展示主体/次级/侵蚀/Perlin FBM；不代表生产云已切换 | `MyRendererCloudNoiseAsset preview` 输出 256×64 PPM，nearest 放大到 1024×256，加 64 px 中文标题栏；见 [`../cloud-noise-assets.md`](../cloud-noise-assets.md) |

| `p1a-cloud-offline-runtime.png` | 同机位 High 档程序化/离线云场在两个展示场景的画面对照 | `cloud-offline-production-acceptance` 的两个 scene 的 high-0/high-1 PNG，各缩为 640×360、两行两栏拼接并加 40 px 中文标题；见 [`../cloud-offline-runtime.md`](../cloud-offline-runtime.md) |

新增素材后在本表补一行：文件名、这张图证明什么、怎么重拍。

| `p1a-ocean-hdri-open.png` | 开放海域使用已有 Kloofendal HDRI；1600 远裁剪面与 2000 范围海面在地平线交接，没有原先的宽灰带 | 按 [`../ocean-hdri-material.md`](../ocean-hdri-material.md) 的 1072×559 固定机位命令重拍，复制 `build-ci-msvc/ocean-hdri-open.png` |

| `p1a-ocean-open-deep-water.png` | 删除 Deep Seabed 后，零实体开放海域使用深水底色及过滤的多尺度细波 | 按 [`../ocean-hdri-material.md`](../ocean-hdri-material.md) 的固定机位命令，将 `waterWaveDiversity` 和 `waterNearMeshFocus` 设为 `0` 重拍 |

| `p1a-ocean-wave-diversity.png` | 与上一图同机位，Hero 使用八组几何波及世界空间相位弯曲，减轻中景长波条带 | 按 [`../ocean-hdri-material.md`](../ocean-hdri-material.md) 的固定机位命令，将 `waterNearMeshFocus` 设为 `0` 重拍 |

| `p1a-ocean-near-mesh-focus.png` | 与上一图同机位，固定顶点重新分配到近景，使短几何波通过网格边长过滤 | 按 [`../ocean-hdri-material.md`](../ocean-hdri-material.md) 的固定机位命令重拍，复制 `build-ci-msvc/ocean-near-mesh-focus-final.png` |

| `p1a-ocean-underwater-wide-after.png` | 2000 单位海面范围下，水下网格边长过滤后的剩余画面与限制 | 按 [`../ocean-underwater-boundaries.md`](../ocean-underwater-boundaries.md) 的固定场景命令重拍 |

| `p1a-ocean-cube-shadow-comparison.png` | 同机位 4× MSAA 下，Cube 水面阴影由块状边缘变为连续边缘 | 按 [`../ocean-cube-shadow.md`](../ocean-cube-shadow.md) 的夹具命令重拍右图；左图为旧采样方式的诊断截图 |

| `p1a-cloud-c7-transport-capture.png` | C7 固定海洋捕获与透射率 LUT 误差曲线；不代表达到写实参考 | `cloud-determinism-acceptance` 后运行 `python tools/CloudCaptureIllustration.py`；见 [`../cloud-c7-contract.md`](../cloud-c7-contract.md) |
