# v3 纹理与固定噪声示例

```powershell
build-ci-msvc/Release/Iris.exe assets/shader-scenes/textures/textures.shaderscene
powershell -NoProfile -File tools/ShaderSceneAcceptance.ps1 -Textures
```

左侧是 sRGB PNG 棋盘格，中间是 raw 3D 数据切片，右侧混合固定 Seed 的 3D / 2D 噪声。时间选择 Z 切片，所有资源由通用运行层读取和上传。JSON / GLSL / PNG / raw 均为 Iris 自制 MIT 示例。PNG 与 raw 的生成命令：

```powershell
powershell -NoProfile -File tools/GenerateShaderSceneTextureFixture.ps1
```

`.shaderscene` v3 沿用 v2 Pass 和 `main` / `mainImage` 入口，新增必需的 `textures` 数组；v1/v2 的严格字段校验保持原样。最多 8 个纹理，ID 与 Pass 共用唯一命名空间，每 Pass 仍最多 4 个通道。声明格式如下：

| 字段 | 支持值 |
| --- | --- |
| dimension / extent | `2d`：[width,height]，每轴 1..2048；`3d`：[width,height,depth]，每轴 1..128 |
| format | `linear-r8`、`linear-rgba8`；二维颜色纹理另支持 `srgb-rgba8` |
| encoding | `raw` 或 `png` 带包内相对 `path`；`hash-v1` 带完整 uint32 `seed` |
| mip | `none` 或 `generate`；资源的全 Mip 链参与预算 |

PNG 支持二维 RGBA8 / sRGB RGBA8，声明尺寸必须与文件一致。raw 为无头 unsigned byte 紧密数组，R8 每 texel 1 byte、RGBA8 每 texel 4 bytes；x 最快、随后 y、最后 z；没有翻转、重排或额外颜色转换。单文件最多 32 MiB，总纹理预算含 Mip 最多 64 MiB。此示例私有纹理预算为 38109 bytes，不包含 program / driver 分配开销与宿主输出。

`hash-v1` 仅生成 R8，按紧密 texel 序号 `index + seed` 的 uint32 值进行固定整数混合，截取低 8 bit；没有随机时钟或进程状态。32³ / Seed 0 的 FNV-1a 字节身份为 `c7c241b345c4317c`，与旧 Enscape 的 volume hash 公式一致；示例 Seed 为 17 / 42。生成器版本、格式、尺寸与 Seed 都属于清单身份。

通道 `source` 可指向纹理 ID；纹理只能声明 `frame: "current"`。`filter` 为 nearest/linear、`wrap` 为 clamp/repeat，三维纹理同时应用于 S/T/R 轴。通道 `mip: "linear"` 需要资源 `mip: "generate"`；`mip: "none"` 不选择 Mip。Buffer 通道仍不支持 Mip，仍按 current/previous 参与执行图。

二维使用 `sampler2D irisChannelN`，三维使用 `sampler3D irisChannelN`，类型不匹配的初始编译和热重载均拒绝。v3 新增 `vec3 irisChannelExtentN`，深度是实际 z 尺寸；旧 `vec2 irisChannelResolutionN` 继续给出 x/y。Image 的 display 编码仍由包完成，sRGB 纹理在读取时解码为线性颜色。

资源在重新打开包时整体准备和提交；失败保留原资源、画面及历史，成功重新打开从清零历史开始。已加载资源是不可变快照，纹理、Manifest、GLSL/include 已支持完整包事务热重载，失败保留当前状态。InputManifest 捕获会核对准备时的清单/文件身份，拒绝变化后的发布输入；同大小、同 mtime 的内容变化也能检测。资源上传恢复原 unpack buffer/layout 和绑定，各 Pass 开始时清理旧通道，帧结束清理所有用到的 2D/3D 通道。

v3 可以保存为 `.myscene` 的相对引用；Raster schema 4 已支持独立 ShaderScene 序列，CPU Job 继续拒绝 GLSL；Enscape 已迁移，仍不提供完整 Shadertoy Uniform 自动注入。见 [当前支持范围](../README.md)。
