# Iris Render Job 示例

CPU/GPU PT Job 使用 `IrisBatch render-sequence <job.renderjob>`；Raster Job 使用 `Iris raster-sequence <job.renderjob>`。工作台 Render Queue 支持 CPU/GPU PT 与 ShaderScene 序列，普通原生 Raster 序列使用 CLI。路径相对 Job 文件解析，`--output` 可覆盖输出模式。已有输出不会默认覆盖。

## Vulkan Raster Baseline

Vulkan 需要使用 `IRIS_ENABLE_VULKAN=ON` 构建的程序与支持 Vulkan 1.3 的驱动。schema 4 显式选择后端：

```json
"renderer": "raster",
"raster": {
  "backend": "vulkan",
  "determinism": true,
  "temporalAccumulation": false,
  "warmupFrames": 0
}
```

`backend` 可选 `opengl` 或 `vulkan`；省略时保留 OpenGL，旧 schema 不接受后端选择。Vulkan 基线输出 Beauty PNG，支持共享静态/刚体场景的基础 PBR、纹理、灯光和 HDRI；不支持 ShaderScene、骨骼动画、Blend/Transmission、云水/大气或 TAA。工作台通过同步回读显示结果，当前不是零拷贝显示路径。

| 示例 | 用途 |
| --- | --- |
| `17_vulkan_baseline.renderjob` | 共享 glTF 材质与纹理、HDRI 和局部灯光，640×360 固定帧 |
| `18_vulkan_rigid_bodies.renderjob` | P1 共享刚体姿态，0～120 帧、30 FPS；需要 Windows/MSVC Release 的 PhysX ON 构建 |

```powershell
Iris.exe raster-sequence assets/renderjobs/17_vulkan_baseline.renderjob
Iris.exe raster-sequence assets/renderjobs/18_vulkan_rigid_bodies.renderjob --output '.local/vulkan/physics-repeat/frame_{frame:04}'
```

Frame sidecar 记录实际后端、Build ID、固定时间、Module、输入身份，以及 Vulkan 的 CPU/GPU 时间、内存分配和验证状态。`deviceLocalBytes` 不包含驱动内部池或工作台 GL 资源，交换链 payload 另列估算。实际效果范围为基础 PBR，阴影、SSAO/TAA/Bloom/NPR 与完整 glTF 材质扩展不包含在此基线。

开发验收需本地 Khronos Validation Layer，使用 `tools/Verify-Vulkan.ps1 -Raster -LayerDirectory <目录>`；普通运行无需安装 Vulkan SDK。缺少 Vulkan 编译能力的程序会明确拒绝 Vulkan Job。

## Ray Query 方向光阴影

`19_ray_query_shadows.renderjob` 使用 Scene 38 的共享地面/两个刚体实例；`20_ray_query_rigid_bodies.renderjob` 使用 Scene 39 的 P1 0～120 帧姿态，需要 PhysX ON。后端仍选择 `raster.backend: vulkan`，Scene 通过 `renderer.vulkanRayQueryShadows: true` 显式启用；`renderer.vulkanShadowDebug: true` 输出白色可见、黑色遮挡的 Mesh 调试图。两个选项默认 false 并随 Scene 保存。

需要设备支持 rayQuery、accelerationStructure 与 bufferDeviceAddress。能力不足或遇到 Skinned、Blend/Transmission、Alpha Mask 时明确拒绝；可关闭 Ray Query 继续基础 Vulkan Raster，或选 OpenGL 使用现有 CSM。当前仅方向光硬阴影；局部灯光仍照常参与着色，未增加光追局部灯光阴影。相同几何的刚体移动只更新 TLAS，sidecar 记录 BLAS/TLAS 数量、每帧 build/update 次数、AS 分配与更新时间。

严格检查使用 `tools/Verify-Vulkan.ps1 -RayQuery -LayerDirectory <目录>`；产品、GUI、P1 和同机位性能入口为 `tools/VulkanRayQueryProductAcceptance.ps1`，独立包可用 `tools/VulkanPackageAcceptance.ps1 -RayQuery` 验证。Vulkan 仍采用同步回读与逐帧 Raster 资源准备，首版没有实时性能承诺。


## GPU Path Tracer 与 Sponza / Stanford

schema 4 设置 `renderer: "gpu-path-traced"`，需要 Vulkan Ray Tracing Pipeline。当前支持共享静态/刚体网格、glTF PBR 纹理、显式灯光、HDRI、发光三角形、NEE/MIS、Transmission 与单活动吸收介质。分辨率每边不超过 1280，SPP 不超过 65536，Depth 不超过 16；GPU Resume、Adaptive/VNDF、Alpha Mask/Blend、Skinned、嵌套介质和透光阴影仍不支持，遇到不支持输入明确拒绝。

| 示例 | 输入与固定预算 |
| --- | --- |
| `21_gpu_path_traced.renderjob` | C1 历史基础示例，保留兼容输入 |
| `22_gpu_sponza_atrium.renderjob` | Intel Sponza 2022 Atrium，160×90、128 SPP |
| `23_gpu_stanford_glass.renderjob` | Stanford Bunny / Dragon 玻璃，160×90、256 SPP |
| `24_gpu_stanford_opaque.renderjob` | 同几何和照明的不透明对照，160×90、256 SPP |

22～24 输出 Beauty、Albedo、Normal、Depth、Direct、Indirect、Sample Count、Variance 的 PNG/EXR 和 GPU 报告。相对资源路径、输入清单、事务发布与取消沿用既有 Job 合同；Queue 使用 IrisBatch 子进程，已有输出不会默默覆盖。

```powershell
./IrisBatch.exe render-frame assets/renderjobs/23_gpu_stanford_glass.renderjob 0 --output .local/renders/stanford/frame
```

上述小尺寸用于数字收敛和输出合同核对。人工画质检查在工作台打开 `assets/benchmarks/m3c/sponza-atrium.myscene` 或 `stanford-glass.myscene`，选择 GPU Path Traced，让画面填满实际 Viewport；1920×1080 工作台会按当前内部最长边 1280 限制显示累积结果。源码中的 `tools/VulkanPathTracingVisualAcceptance.ps1` 自动等待 64 SPP 完成并捕获三场景的大画幅。资产派生和研究/商业使用范围见 `assets/benchmarks/m3c/README.md` 及根目录 `ASSET_LICENSES.md`。


## 原生720p时间处理交付示例

25～27 为 Sponza、Stanford Glass、Opaque 的 schema4 GPU 时间处理示例：1280×720、当前Raw4SPP、Depth4、固定Seed20261026、两帧，输出Raw/Accumulated/Denoised PNG/EXR和报告。根字段 `gpuTemporalDenoise: true` 显式启用时间处理；默认Scene和旧Job不变。静止相同Raw不会虚增采样；透射仍保留Raw，Depth4玻璃比Depth8有截断误差，空间处理可能软化石材。Depth4是显式探索档位，不是同质量替代或30FPS承诺。

```powershell
./IrisBatch.exe render-sequence assets/renderjobs/26_gpu_temporal_stanford_glass.renderjob
```

工作台Renderer页可手动设SPP/Depth并选择时间与空间复选框。帧率应依据持续采样/运动的整帧数据，不能读取停采后的UI FPS作为路径追踪吞吐。研究使用与再分发范围遵循相邻资产许可。
