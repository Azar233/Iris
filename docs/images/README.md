# docs/images：回归基线清单

本目录是**回归基线的版本化清单**：这里的每张 PNG 都是人工复核过的渲染器输出，由 visual-regression 目标逐像素比对。比对使用容忍 GPU 差异的 MAE 与 changed-pixel 阈值，而不是字节相等——比对的默认阈值为 MAE `0.015` / changed fraction `0.08`，单像素最大通道差超过 `8` 才计入 changed，各套件可以收紧或放宽这两个数字（见每节标注）。写作规范见 [`../README.md`](../README.md) 第 3 节。

## 本目录的规则

- **说明性截图不属于这里。** 解释功能、UI 状态或前后对照的截图放 [`../media/`](../media/)，它们不参与任何自动比对，可以自由重拍。
- **把一张图放进本目录，就等于把它变成基线。** 之后的运行必须与它匹配；只能在明确批准的基线更新中改写，并在同一次变更里同步本清单与 [`../regression-baseline-audit.md`](../regression-baseline-audit.md)。
- 不确定该放哪一类时放 `docs/media/`：把说明性截图塞进 `docs/images/` 会污染回归基线。
- 新增或改写基线时，本清单要给出「文件名 + 机位/参数 + 这张图证明什么」，并保证图能按记录的命令重拍。

## 当前基线状态

### 2026-09-16：重新锚定到 Kloofendal 环境

Raster 基线在 2026-09-16 重新锚定到内置的 `kloofendal_48d_partly_cloudy_puresky_4k.exr` 环境：此前使用的 Delta 2 资产已被移除，天空像素、Split-Sum IBL、反射与曝光响应随之改变，阈值没有被放宽来掩盖这次固定输入变化。采集会显式压制编辑器选中描边（`MYRENDERER_HIDE_SELECTION_OUTLINE=1`）。验收参考机为 NVIDIA GeForce RTX 4060 Laptop GPU、驱动 591.44、OpenGL 3.3.0；独立的全套重跑对每张图都报出 MAE `0` 与 changed `0%`。原因、旧/新差异与基础设施改动见 [`../regression-baseline-audit.md`](../regression-baseline-audit.md)。

### 2026-09-20：级联阴影（P1-A 切片 3）改变了阴影

方向光阴影从单个正交盒换成 3 级级联，因此**所有包含阴影的基线都被重新采集**：Glass-3 的 6 张与 Glass-4 的 14 张，共 20 张。这不是固定的输入或环境变化，而是阴影本身的质量变化，量化对照如下（以 Glass-3 固定水晶场景为例）：

| | 旧单框 | 级联第 0 级 |
| --- | --- | --- |
| 阴影 texel 尺寸 | 约 `0.0221` 世界单位 | 约 `0.00628` 世界单位（细 `3.5×`） |
| 覆盖半径 | ±4（宽 8） | 约 ±10.3（宽 20.6，宽 `2.6×`） |

审查确认变化只出现在阴影及其内部的焦散读数上：没有级联接缝、没有几何或折射变化。触发这次更新的漂移为 `glass3_lightspace_msaa1` MAE `0.00222` / changed `10.1%` 与 `glass4_volume_glass_off` MAE `0.00603` / changed `8.16%`。参数为级联数 3、`lambda` 0.75，见 `RendererSettings::shadowCascadeCount` 与 `shadowCascadeSplitLambda`；实现与两道验证关口见 [`../shadow-cascades.md`](../shadow-cascades.md)。

更新后 `renderer-regression-suite` 全部套件通过。

## Prism：光谱与光束基线

Prism-0 ～ Prism-4（历史基线；当前 `tools/` 中没有按文件名引用它们的脚本，重拍入口待复核）：

- `prism0_baseline.png`：Prism-0 固定机位、黑场舞台、原始封闭三棱柱，以及光学路径求解前的白色入射光束占位。
- `prism1_optical_path.png`：Prism-1 中心波长结果，入射、内部与出射线段由 CPU prism solver 生成。
- `prism2_continuous_spectrum.png`：Prism-2 21 采样连续光谱，使用波长相依的 Cauchy IOR 与归一化 HDR 线能量。
- `prism2_seven_band.png`：Prism-2 七波段美术方向模式，使用同一套双界面光学求解器。
- `prism3_continuous_ribbon.png`：Prism-3 把连续光谱渲染为面向相机的软边 HDR ribbon mesh。
- `prism3_seven_band_ribbon.png`：Prism-3 美术方向离散 ribbon，使用加性 HDR 混合与 bloom。
- `prism4_exaggerated_cover.png`：Prism-4 Exaggerated Cover preset，固定 hero 机位、七波段光谱、白点 tint 与光束 bloom。
- `prism4_optical_debug.png`：Prism-4 光路 overlay，显示波长路径、界面点以及入射/出射表面法线。

Prism-5（`prism5-visual-regression`，阈值 MAE `0.015` / changed `0.08`）：

- `prism5_white_beam_no_prism.png`：白光展示舞台，棱镜隐藏，所有波长路径重合（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_SHOW_MODEL=0 MYRENDERER_PRISM_IOR=1 MYRENDERER_PRISM_DISPERSION=0`）。
- `prism5_prism_no_dispersion.png`：可见棱镜但关闭色散（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_DISPERSION=0`）。
- `prism5_continuous_21.png`：物理连续光谱模式（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_SAMPLES=21`）。
- `prism5_seven_band.png`：美术方向光谱模式（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_SPECTRUM_MODE=seven`）。
- `prism5_tir_debug.png`：高 IOR 全内反射（total internal reflection）调试证据（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_PRESET=2 MYRENDERER_PRISM_DEBUG=1`）。
- `prism5_angle_minus_8.png`：世界空间光束角度与棱镜面拓扑覆盖，`-8°`（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_BEAM_ANGLE=-8`）。
- `prism5_angle_plus_12.png`：同一覆盖的另一侧角度，`+12°`（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_BEAM_ANGLE=12`）。
- `prism5_msaa1.png`：固定机位边缘采样的 1x MSAA 一侧（`MYRENDERER_MSAA=1`）。
- `prism5_msaa4.png`：固定机位边缘采样的 4x MSAA 一侧（`MYRENDERER_MSAA=4`）。
- `prism5_hero_exaggerated.png`：最终作品集 Hero Shot（`MYRENDERER_MSAA=4 MYRENDERER_PRISM_PRESET=3`）。

## Glass：折射、体积吸收与焦散

Glass-2B（历史基线；当前 `tools/` 中没有按文件名引用它们的脚本，重拍入口待复核）：

- `glass2b_geometric_thickness.png`：Glass-2B Thickness 调试视图，使用前/后表面深度而不是统一材质代理。
- `glass2b_uniform_fallback.png`：同机位、同调试视图，关闭几何厚度，保留材质因子/纹理回退。
- `glass2b_front_back_debug.png`：入射/出射深度有效性视图；绿色/黄色像素表示存在有效的正深度跨度，品红标记回退区域。

Glass-2C（`glass2c-visual-regression`，阈值 MAE `0.015` / changed `0.08`，夹具 `assets/models/glass_volume_sphere.gltf`，1920×1080）：

- `glass2c_msaa1.png`：固定机位曲面体积玻璃舞台，含两个独立物体、棋盘格畸变、HDRI 反射与厚度相关橄榄色吸收，1x MSAA（`MYRENDERER_MSAA=1`）。
- `glass2c_msaa4.png`：同一舞台的 4x MSAA 对照（`MYRENDERER_MSAA=4`）。
- `glass2c_approximate.png`：同机位，改用 Glass-2B 的局部平行出射近似，作为双界面折射 On/Off 对照。
- `glass2c_thickness.png`：闭合球夹具的几何路径长度输出（`MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=5`）。
- `glass2c_transmittance.png`：同一夹具的 Beer-Lambert 透射率输出（`MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=6`）。
- `glass2c_exit_normal.png`：采样的曲面出射法线；品红标识文档中记录的屏幕空间回退区域（`MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=9`）。
- `glass2c_object_id.png`：重叠玻璃实例的稳定 per-RenderItem 配对颜色（`MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=10`）。

Glass-3（`glass3-visual-regression`，阈值 MAE `0.015` / changed `0.08`）：

- `glass3_lightspace_msaa1.png`：固定水晶、白色接收面、RGB Photon Splat 聚焦与空间滤波基线，1x MSAA（`MYRENDERER_MSAA=1 MYRENDERER_CAUSTICS_MODE=1`）。
- `glass3_lightspace_msaa4.png`：同一基线的 4x MSAA 对照（`MYRENDERER_MSAA=4 MYRENDERER_CAUSTICS_MODE=1`）。
- `glass3_caustics_off.png`：同机位关闭 caustics，玻璃与彩色透射阴影保持启用（`MYRENDERER_MSAA=4 MYRENDERER_CAUSTICS=0`）。
- `glass3_projector.png`：美术方向 HDR Projector / Decal 模式（`MYRENDERER_MSAA=4 MYRENDERER_CAUSTICS_MODE=0`）。
- `glass3_caustics_debug.png`：滤波后 caustics map 的原始接收面采样（`MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=11`）。
- `glass3_transmission_shadow.png`：经过 PCF 与乘性 Beer-Lambert 阴影合成后的 RGB 透射可见性（`MYRENDERER_MSAA=4 MYRENDERER_GLASS_DEBUG=12`）。

Glass-4 Volume，双球 KHR volume 验证场景（`glass4-visual-regression`，阈值 MAE `0.015` / changed `0.08`；默认采集为 1920×1080、`MYRENDERER_MSAA=4`、`MYRENDERER_TRANSMISSION=1`、`MYRENDERER_GEOMETRIC_THICKNESS=1`、`MYRENDERER_TWO_INTERFACE_REFRACTION=1`、`MYRENDERER_VOLUME_THICKNESS_SCALE=1.0`、`MYRENDERER_GLASS_PRESET=1`、`MYRENDERER_IOR=1.5`、`MYRENDERER_DISPERSION_ENABLED=0`、`MYRENDERER_CAUSTICS=0`）：

- `glass4_volume_final.png`：最终画面，即默认参数与 4x MSAA 的组合。
- `glass4_volume_glass_off.png`：关闭透射（`MYRENDERER_TRANSMISSION=0`）。
- `glass4_volume_ior_low.png`：低 IOR（`MYRENDERER_IOR=1.1`）。
- `glass4_volume_ior_high.png`：高 IOR（`MYRENDERER_IOR=1.8`）。
- `glass4_volume_thickness_low.png`：低厚度缩放（`MYRENDERER_VOLUME_THICKNESS_SCALE=0.35`）。
- `glass4_volume_attenuation_clear.png`：无吸收的清晰玻璃 preset（`MYRENDERER_GLASS_PRESET=0`）。
- `glass4_volume_exit_normal.png`：真实出射法线调试视图（`MYRENDERER_GLASS_DEBUG=9`）。
- `glass4_volume_object_id.png`：玻璃实例配对颜色（`MYRENDERER_GLASS_DEBUG=10`）。
- `glass4_volume_approximate.png`：关闭双界面折射的近似模式（`MYRENDERER_TWO_INTERFACE_REFRACTION=0`）。
- `glass4_volume_msaa1.png`：1x MSAA 覆盖（`MYRENDERER_MSAA=1`）；4x 一侧即默认 4x MSAA 的 `glass4_volume_final.png`。

Glass-4 Caustics，固定机位 Crystal hero 覆盖（`glass4-visual-regression`，阈值与默认参数同上）：

- `glass4_caustics_final.png`：最终画面，包含色散与 caustics。
- `glass4_caustics_dispersion_off.png`：关闭色散（`MYRENDERER_DISPERSION_ENABLED=0`）。
- `glass4_caustics_off.png`：关闭 caustics（`MYRENDERER_CAUSTICS=0`）。
- `glass4_caustics_msaa1.png`：1x MSAA 覆盖（`MYRENDERER_MSAA=1`）。

## Deferred、局部光、Instancing、屏幕空间与蒙皮

GP-P1 Deferred 与 G-Buffer（`deferred-visual-regression`，阈值 MAE `0.015` / changed `0.08`）：

- `gp_p1_forward_final.png`：Forward 路径的最终画面。
- `gp_p1_deferred_final.png`：Deferred 路径的最终画面；与上一张使用同一 PBR fixture、机位、HDRI、方向光与 4x MSAA，用于渲染路径一致性对照。
- `gp_p1_gbuffer_albedo.png`：原始 MRT attachment 的 Albedo 视图，绕过 skybox、overlay、透明与 post FX。
- `gp_p1_gbuffer_normal.png`：同一帧的 Normal attachment 视图。
- `gp_p1_gbuffer_material.png`：同一帧的 Material attachment 视图。
- `gp_p1_gbuffer_depth.png`：同一帧的 Depth attachment 视图。

GP-P1B Local Lights（`local-lights-visual-regression`，阈值 MAE `0.015` / changed `0.08`）：

- `gp_p1b_forward_lights8.png`：低档灯光下 Forward 一侧，相同 100 物体舞台，4 个 point + 4 个 spot。
- `gp_p1b_deferred_lights8.png`：低档灯光下 Deferred 一侧，机位与舞台相同。
- `gp_p1b_forward_lights64.png`：高档灯光下 Forward 一侧，32 个 point + 32 个 spot。
- `gp_p1b_deferred_lights64.png`：高档灯光下 Deferred 一侧；这一对是高挡 Forward/Deferred 一致性与可扩展性证据。

GP-P1C Instance Stress（`instance-stress-visual-regression`，阈值 MAE `0.015` / changed `0.08`）：

- `gp_p1c_baseline.png`：固定 2,500 球场景，接入 GPU Instancing、CPU Frustum Culling 与投影尺寸 LOD 之前。
- `gp_p1c_optimized.png`：同一场景完成 GPU Instancing、CPU Frustum Culling 与投影尺寸 LOD 之后。

GP-P1D Screen Space（`screen-space-visual-regression`，阈值 MAE `0.02` / changed `0.10`）：

- `gp_p1d_baseline.png`：固定 Deferred 场景在接入 SSAO 之前。
- `gp_p1d_ssao_final.png`：同一场景完成仅环境光（ambient-only）合成之后。
- `gp_p1d_ssao_debug.png`：原始深度感知遮蔽（depth-aware occlusion）视图。
- `gp_p1d_taa_static.png`：1x-MSAA 时序解析，静止相机。
- `gp_p1d_taa_motion.png`：1x-MSAA 时序解析，确定性旋转相机。
- `gp_p1d_motion_vectors.png`：重投影运动向量诊断视图，用于检查 disocclusion。
- `gp_p1d_history_weight.png`：接受历史权重诊断视图，用于检查 ghosting。

GP-P1E Skinning（`skinning-visual-regression`，阈值 MAE `0.01` / changed `0.04`）：

- `gp_p1e_bind_pose.png`：原始三关节 glTF fixture，动画关闭。
- `gp_p1e_animated_pose.png`：同一 fixture 在 Wave clip 的 1 秒姿态采样。
- `gp_p1e_joint_debug.png`：插值 joint palette 影响颜色诊断。
- `gp_p1e_weight_debug.png`：主顶点权重（dominant vertex weight）诊断。

## SR-P0 场景基础

`foundation-visual-regression`，阈值 MAE `0.02` / changed `0.10`，固定 1920×1080：

- `sr_p0_scene_entities.png`：至少 10 个 Entity 共享一份 Cube Mesh。
- `sr_p0_object_motion.png`：刚体运动向量调试图（`cube.obj`）。
- `sr_p0_skin_motion.png`：骨骼运动向量调试图（`skinning_test.gltf`，动画帧步长 `0.18`）。

## P0-D 降噪三联图

由 `path-tracing-denoising-experiment` 生成，从左到右固定为 **Raw 4 SPP / AOV A-Trous / 2048 SPP Reference**；指标与失败边界见 [`../p0-d-sampling-denoising.md`](../p0-d-sampling-denoising.md)。

- `p0d-diffuse-4spp-triptych.png`：Diffuse 室内场景，证明 AOV A-Trous 降低低 SPP 噪声且收益主要来自 Indirect 通道。
- `p0d-pbr-hdri-4spp-triptych.png`：PBR + HDRI 高动态范围场景，证明三组场景中该场景的 RMSE/SSIM 改善最大。
- `p0d-instancing-4spp-triptych.png`：Instancing 几何边缘压力场景，证明场景本身干净时滤波收益有限、Direct 通道反而变差。

待复核：当前 `tools/` 与 `CMakeLists.txt` 中未见按文件名比对本组三联图的入口（`path-tracing-denoising` 测试只运行 `MyRendererDenoisingTests`），本组目前是版本化的阶段证据图，清单登记为基线的依据需要确认。

## 展示与参考图（分类待复核）

以下图片版本化在 `docs/images/`，但按 [`../README.md`](../README.md) 第 3 节的分类属于说明性插图（UI、展示与外部参考），它们的重拍命令与归类需要确认；本次没有移动任何文件。

- `p1-workspace-reference.png`：用户提供的 P1 GUI / Workspace 信息架构参考，只用于约束大视口、Scene Explorer / Inspector、底部多标签工作区与紧凑工具栏的方向；它不是渲染回归基线，也不要求像素级复刻。具体可借鉴项与禁止照搬项见 [`../../todolist.md`](../../todolist.md) 的「P1 GUI / Workspace 参考与约束」。
- `editor-ui-overview.png`：Iris 编辑器总览，在真实 OpenGL/ImGui 帧完成后捕获整个编辑器窗口，而不是只保存 Viewport 纹理；它支撑根 `README.md` 的 Editor UI 设计规范（`#0E0F10` → `#202126` 的中性黑灰分层，`#4D9EFF` 只用于选中/激活/拖拽反馈）。重拍：`MYRENDERER_SMOKE_TEST=1`、`MYRENDERER_EDITOR_SCREENSHOT=docs/images/editor-ui-overview.png`，运行 `.\build-mingw\Iris.exe .\assets\models\cube.obj`。
- `editor-ui-renderer-drawers.png`：Renderer 抽屉与两列属性布局；仓库中没有记录它的重拍命令（待补）。
- `polyhaven-studio-lounge.png`：`13_polyhaven_studio_lounge` 展示场景（暖色室内陈列，验证复杂 glTF 材质、Alpha 植物、局部灯光、阴影与构图）的截图，用于根 `README.md` 展示；仓库中没有记录它的重拍命令（待补）。
- `polyhaven-material-gallery.png`：`14_polyhaven_material_gallery` 中性材质展台（集中验收织物/木材、石材、陶瓷和氧化金属）的截图，用于根 `README.md` 展示；仓库中没有记录它的重拍命令（待补）。
