# Orbit ShaderScene

最小自制单 Image 包；只增加 JSON 与 GLSL，不增加场景专用 C++ Renderer。
作者：Iris contributors；源码遵循本目录 `LICENSE` 的 MIT License。

```powershell
build-ci-msvc/Release/Iris.exe assets/shader-scenes/orbit/orbit.shaderscene
```

也可使用 Open Scene 或将 `.shaderscene` 拖入窗口；Save Scene 保存 `.myscene` 相对包引用，重开恢复同一包。

v1 描述文件严格校验字段，固定 `entry: main`、`output: display-rgba8`、`camera: project`。
`resourceRoot` 相对于 manifest，fragment/include 必须位于规范化后的资源根内；不会向包外的祖先目录搜索 include。
GLSL 使用 `#version 330 core` 与 `void main()`，负责给完整画面写入 display 编码 RGBA；宿主不再执行 tone map。
可选输入：`irisTime`（float，秒）、`irisResolution`（vec2，像素）、`irisCameraPosition`（vec3）、`irisView` / `irisProjection`（mat4）。
公开参数目前支持最多 16 个有界 float，ID 对应同名 uniform，读取包内 default；修改 default 后重新打开包。
活跃 uniform 类型必须匹配；编译或类型检查失败保留整个旧运行版本。

本 v1 包提供预览及固定 PNG 捕获。多 Pass/反馈与帧序号见 [v2 示例](../feedback/README.md)，mainImage 见 [入口示例](../main-image/README.md)，纹理与固定噪声见 [v3 示例](../textures/README.md)。参数 Inspector/覆盖值、关联文件树和 schema 4 序列 Job 已接通，见 [当前支持范围](../README.md)。鼠标 uniform 与模型材质 Shader 尚未实现。
每帧 Image 输出先清为不透明黑色，discard 区域不会隐式保留上一帧。
