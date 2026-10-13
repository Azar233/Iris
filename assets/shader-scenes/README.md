# ShaderScene 首版支持范围

Iris 使用项目共享 Camera、Timeline、Job 与 GL 上下文运行全屏 fragment 包。新包只需描述文件、GLSL 和资源；[Orbit](orbit/README.md) 是最小单 Pass 模板，[sequence](sequence/sequence.shaderscene) 是最小反馈/序列模板。可以从 Content Browser 打开包，查看声明文件、参数、Pass/Channel、历史和 GPU 时间；源码由外部编辑器编辑。

| 版本 | 支持 |
| --- | --- |
| v1 | 单 Image，`main()`，display RGBA8，float 参数 |
| v2 | 最多 4 Pass / 每 Pass 4 Channel，当前帧与显式 previous，全/半尺寸，Buffer linear RGBA16F，`main` 或 `mainImage` |
| v3 | 最多 8 个二维/三维纹理，PNG/raw/hash-v1，linear R8/RGBA8、二维 sRGB、Mip；bool 参数及可选 CPU parameterSource |

声明的 GLSL uniform 由宿主提供；编译器优化掉的未使用 uniform 可省略。类型必须匹配。

| uniform | 类型与语义 |
| --- | --- |
| `irisTime`, `irisTimeDelta` | float，绝对秒及实际历史步长，重置后 delta=0 |
| `irisFrame`, `irisHistoryValid`, `irisSeed` | int / bool / int，重置后帧 0、历史无效；Job Seed 范围 0～2147483647，GUI 默认 0，纹理 Seed 独立声明 |
| `irisResolution`, `irisViewportResolution` | vec2，当前 Pass 尺寸与最终输出尺寸 |
| `irisMouse` | vec4，xy 为最终 Viewport 像素坐标（左下原点）；z 为按键位掩码（左=1/右=2/中=4）；w 为输入有效标志。无输入全零，半尺寸 Pass 不缩放 xy |
| `irisChannel0..3` | sampler2D / sampler3D，由通道声明的资源决定 |
| `irisChannelResolution0..3`, `irisChannelExtent0..3` | vec2 / vec3，实际资源尺寸；Extent 是 v3 输入 |
| `irisCameraPosition/Forward/Right/Up` | vec3，项目 Camera |
| `irisView`, `irisProjection` | mat4，项目 Camera 矩阵 |
| `irisCameraTanHalfFov`, `irisCameraNear` | float，项目 Camera 参数 |
| 包公开参数 ID | float / bool，当前共享覆盖值或声明默认值 |

`mainImage(out vec4 color, in vec2 coord)` 的 coord 来自 `gl_FragCoord.xy`，不翻转 Y；每次调用需写满 RGBA。宿主不自动注入 Shadertoy 的 `iTime/iResolution/iChannel/iMouse`。`irisMouse` 使用当前按键而非 Shadertoy 点击起点/负号约定；仅 Viewport 图像内且没有 UI 捕获时有效，离开/隐藏后归零。实际使用鼠标的包在输入改变时清历史；未使用的包不受鼠标影响。Scene 不保存鼠标，Queue/CLI 固定 zero-input 并记录报告；交互轨迹录制/离线回放尚未支持。见 [鼠标模板](mouse/README.md)。移动/旋转项目 Camera 仍使用编辑器已有操作。未支持音频、cubemap 通道、Compute Shader、任意 Shadertoy 自动导入、节点图或模型材质 Shader。

同帧循环、缺失文件、非法通道和类型会拒绝候选；成功完整包替换清空反馈，失败保留旧实例。history 策略可保存参数/相机是否清历史和最大时间步；resize、包替换、倒退、策略变化和 Clear 始终重置。保留反馈没有运动重投影。热重载监听 mtime/大小，保留二者的内容修改需手动 Recompile。

schema 4 Job 显式启用 ShaderScene 序列，只支持 Beauty PNG；固定 FPS、Seed、预热和执行顺序，从清空反馈开始，GUI Queue 在 Windows 子进程中执行。HDR/AOV、Resume、Cache、Module 和继续失败策略执行前拒绝。GUI 任意跳帧不自动重放此前的反馈。

```powershell
Iris.exe assets/shader-scenes/orbit/orbit.shaderscene
Iris.exe raster-sequence assets/renderjobs/08_shader_scene_sequence.renderjob
Iris.exe raster-sequence assets/renderjobs/09_enscape_shader_sequence.renderjob
```

上述相对路径适用于解压目录；序列默认写 `.local/render-jobs`，可用 `--output` 指定新的输出位置。输出已存在会拒绝；反馈序列复现需相同初始状态与输入。Windows MSVC/GCC Release 与 NVIDIA OpenGL 3.3 为当前验证组合，其他驱动/平台未保证逐像素一致。

自制包按相邻 MIT 许可分发；[Enscape](enscape/README.md) 保留 Thomas / @Thomas_ensc、TDM 署名和 CC BY-NC-SA 3.0，项目 MIT 不改写其许可。完整资产来源见根目录 ASSET_LICENSES.md。
