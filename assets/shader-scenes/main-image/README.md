# mainImage 入口示例

这个自制 v2 包与 v1 Orbit 使用同一公式、Camera、时间和参数，区别是每个 Pass 提供 `void mainImage(out vec4 color, in vec2 coord)`，宿主生成 `main()`。打开：

```powershell
build-ci-msvc/Release/Iris.exe assets/shader-scenes/main-image/main-image.shaderscene
```

清单 `entry: "mainImage"` 对所有 Pass 生效；每份 fragment 保留 `#version 330 core`，使用现有 `irisTime`、`irisResolution`、`irisCameraPosition` 和包内参数。不要再声明 `main()` 或 fragment output；`coord` 是 `gl_FragCoord.xy`，没有 Y 翻转，每次调用必须写全 `color` 的四个分量。Image 写 display 编码的 RGBA8，Buffer 写线性 RGBA16F。入口包装不会新增 Pass、纹理或颜色转换。

v1 仍仅接受 `main`；v2 仍支持手写 `main`。这不是完整 Shadertoy API：没有自动注入 `iTime` / `iResolution` / `iChannel`，外部纹理与 Enscape 迁移继续按路线图推进。GLSL 编译、Uniform 类型、包内 include 和原子重载检查同时适用于两个入口。

```powershell
powershell -File tools/ShaderSceneAcceptance.ps1 -BuildDirectory build-ci-msvc -MainImage
cmake --build build-ci-msvc --config Release --target shader-scene-gpu-acceptance
```

JSON 和 GLSL 使用本目录 MIT 许可。
