# Enscape 四 Pass ShaderScene

`enscape.shaderscene` 使用通用 v3 运行层执行 A → B → C → Image。A/B 使用单缓冲，C 显式读取上一帧并使用双缓冲；历史无效时 C 读取当前 B，与保留的旧渲染路径一致。Weather 是 256×256 R8 raw，使用生成 Mip；Noise 是 32³ R8 `hash-v1` / Seed 0。该包的 GLSL 与适配遵循相邻 [LICENSE.md](LICENSE.md) 中的 CC BY-NC-SA 3.0。

```powershell
build-ci-msvc/Release/Iris.exe assets/scenes/01_ocean_clouds_hero.myscene
build-ci-msvc/Release/Iris.exe assets/scenes/03_enscape_ocean_study.myscene
build-ci-msvc/Release/Iris.exe assets/shader-scenes/enscape/enscape.shaderscene
cmake --build build-ci-msvc --config Release --target enscape-shader-scene-acceptance
powershell -NoProfile -File tools/EnscapeShaderSceneAcceptance.ps1
```

01 隐藏方块并启用 `noiseReduction`；03 保留研究方块及原降噪默认值。两者通过可选 `renderer.enscapeShaderSceneResource` 引用包，旧 `enscapeCubeShaderEnabled` 控制启用/关闭，已有 13 个参数继续存储在共享 `RendererSettings.enscapeCube`。没有此资源引用的旧 Scene 仍走原静态插件，不自动迁移。

v3 可声明 `parameterSource: {"pluginId":"iris.enscape-study","schemaVersion":1}`，按已有 CPU schema 获取参数；ID、类型和范围必须匹配，不保存第二份实时值。新增 `bool` 参数只接受 JSON boolean 的 `default`，不带 min/max；原 float 格式不变。未声明 source 的自制包仍使用声明初始值。直接打开这个包使用共享设置默认值；查看 01/03 的既有相机及参数应打开对应 Scene。

宿主提供 `irisCameraForward/Right/Up`、`irisCameraTanHalfFov`、`irisCameraNear`；其余时间、分辨率、帧序号和历史输入沿用 [Feedback 约定](../feedback/README.md)。角度参数在包内转成太阳方向。参数变化会清历史，未提供运动重投影。

固定图门槛为 RGB MAE ≤ 0.0005、最大 RGB 差超过 2/255 的像素比例 ≤ 0.002；序列门槛为 MAE ≤ 0.001、比例 ≤ 0.005。GPU P95 ≤ 旧路径 × 1.15 + 0.1 ms；CPU P95 ≤ 旧路径 + 0.30 ms；私有纹理预算必须相等。测试覆盖两 Scene、降噪开关、暂停重绘、24 帧重放、参数/相机/水下、候选拒绝及同轮交替成本测量，不改旧基线。

Weather 可按原算法重生成，再核对 SHA256：

```powershell
cmake --build build-ci-msvc --config Release --target IrisEnscapeWeatherGenerator
build-ci-msvc/Release/IrisEnscapeWeatherGenerator.exe output/weather.raw
Get-FileHash output/weather.raw
```

65536 bytes 的 SHA256 是 `9D00A62E5B1496A6F7CC12ACC1035FC50C6442CC5CD42EEB94F7A548C78A8943`。原纹理不是随原 Shadertoy 源码提供的素材；这里保留 Iris 的确定性替代数据。

已有 07 Raster Job 保留支持并记录包资源身份。独立 schema 4 ShaderScene Job、公开参数覆盖界面、文件树/诊断定位已接通，见 [当前支持范围](../README.md)。鼠标 uniform 与模型材质 Shader 尚未实现；整项 A3 保持未完成。
