# 模型 ShaderMaterial 示例

`checker.shadermaterial` 是 v1 模型片元材质包。通过对象 Inspector 的 Shader material 绑定包路径；公开参数为 Gain 和 Checker frequency。场景示例是 `assets/scenes/fixtures/29_shader_material.myscene`，固定输出任务是 `assets/renderjobs/11_shader_material.renderjob`。

在 Assets 的 ShaderScenes 分类选择或拖入 `.shadermaterial`，工作台只浏览包的源码、include、README 与许可证，保持当前 Scene。选择目标模型后点击 Bind to selected model 明确应用；Recompile active package 编译完整包。失败诊断可定位到 include 的原文件和行号，保留上一版材质；修复后再次编译。未选择模型时绑定按钮禁用。参数和清除绑定仍在对象 Inspector，使用 Back to scene Inspector 返回。

当前合同：原生 Raster 的 Forward、静态不透明模型、关闭 TAA。一个 Entity 的全部不透明 submesh 共用一个包，仍从导入的 `ModelData` 读取材质因子和纹理；清除后恢复原生材质。Deferred、CPU PT、Module 场景、蒙皮、Mask/Blend/Transmission 会明确拒绝。版本和旧 Scene 格式保持兼容。

包沿用 ShaderScene 的根目录与 include 安全边界、作者与许可证和 float 参数校验，使用 `.shadermaterial` 扩展名、`entry: main`、`camera: project` 与 `output: linear-rgba16f`。不得绑定为全屏 ShaderScene。参数 ID 不得占用 `u`、`gl_`、`iris` 宿主前缀。

宿主复用 `basic.vert`，提供 `vWorldPosition`、`vWorldNormal`、`vTexCoord0`、`vWorldTangent`；提供 `uCameraPosition`、`irisTime`（秒）、`irisResolution`（输出尺寸）以及导入材质的 `uBaseColor`、`uBaseColorTexture`、`uNormalTexture`、`uHasNormalTexture`、`uMetallicRoughnessTexture`、`uMetallicFactor`、`uRoughnessFactor`。颜色纹理由原生上传路径执行 sRGB 解码，片元输出线性 RGB，显示编码继续在原生后处理执行一次。`FragColor.a` 应为 1。

这是片元覆盖；顶点变形、独立灯光/阴影接收 API、节点图和自定义透明路径不在首版范围内。几何深度与投影阴影继续使用原生模型。示例提供自定义方向光响应，不宣称与原生 PBR 光照等价。

重载按完整包事务发布，错误保留上一版程序和参数；自动重载关闭时保持当前程序，修复文件或点击 Recompile 可再次编译。最多 32 个活动包。固定 Raster Job 禁止自动重载，报告记录模型 Entity、包、有效参数及已消费的资源指纹；CPU Job 不回退到旧 PBR 材质。

```powershell
cmake --build --preset msvc --target shader-material-gpu-acceptance
./tools/ShaderMaterialWorkspaceAcceptance.ps1
./build-ci-msvc/Release/Iris.exe raster-sequence ./assets/renderjobs/11_shader_material.renderjob --output "E:/MyRenderer/.local/verification/a3m1/job/frame_{frame:04}"
```

示例由 Iris contributors 原创，MIT；完整许可证见 LICENSE。
