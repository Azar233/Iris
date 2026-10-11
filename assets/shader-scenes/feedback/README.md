# Feedback ShaderScene

自制三 Pass 包，只有 JSON 与 GLSL；作者 Iris contributors，MIT License。
Manifest 故意按 Image/Blur/Accum 声明，CPU 按本帧依赖排序为 Accum/Blur/Image。
Accum 在半分辨率线性 RGBA16F 中读取自身上一帧，Blur 读取本帧 Accum，Image 写 display RGBA8。

```powershell
build-ci-msvc/Release/Iris.exe assets/shader-scenes/feedback/feedback.shaderscene
```

v2 最多 4 个 Pass（含唯一 Image）、每 Pass 最多 4 个 slot 0..3 的二维 Pass 通道。
通道 frame 为 current/previous；filter 为 nearest/linear；wrap 为 clamp/repeat；mip 固定 none。
只能引用 buffer，禁止引用 Image。本帧循环拒绝；显式上一帧可引用自身或其他 buffer。
Buffer 固定 linear-rgba16f，sizeDivisor 为 1/2（奇数向上取整）；Image 固定全尺寸 display-rgba8。
私有双缓冲纹理合计上限 256 MiB，extent 1..8192 且不超过设备 GL 限制。

输入沿用 v1，增加 irisViewportResolution vec2、irisTimeDelta float、irisFrame int、irisHistoryValid bool；
irisResolution 是当前 Pass 尺寸；irisChannel0..3 为 sampler2D，irisChannelResolution0..3 为对应输入 vec2。
所有 buffer 在首次帧与历史失效时两侧清零；每个输出在绘制前清零。重复时间使用同一上一帧侧重绘，反馈不会累计。
时间倒退、步长 >0.25 s、Camera/投影/尺寸变化、显式失效与成功编译发布清零；连续前进只在整帧边界交换双缓冲。
这是颜色反馈，没有运动重投影。Manifest/default 更改需重新打开；源码使用已有整批事务重载，失败保留全部旧程序和历史。

外部 2D/3D 纹理、固定噪声资源、mainImage 包装与 Enscape 迁移属于后续 S1-A2 工作；工作台文件树、参数覆盖 UI、Job 与模型材质 Shader 仍未实现。
