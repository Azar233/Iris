# 鼠标输入模板

打开 `mouse.shaderscene`，在 Viewport 图像内移动、按住左/右/中键，观察光标与反馈颜色。`Accum` 是半尺寸 feedback；两个 Pass 接收相同的最终输出像素坐标。右键旋转与中键平移仍属于项目 Camera 操作。

`irisMouse` 为 vec4：xy 是左下原点的最终 Viewport 像素坐标，z 是当前按键位掩码（左=1、右=2、中=4），w 是图像区域内输入有效标志。离开图像、UI 捕获、隐藏 Viewport 或离线 Job 时全部为零。它不使用 Shadertoy 点击起点/负号语义。

实际使用该 uniform 的包在鼠标改变时清空历史；稳定输入按时间正常推进。Scene 不保存鼠标，Queue/CLI 一律使用固定零输入，报告记录 zero-input。交互录制/回放与 Job 自定义鼠标轨迹不在当前支持范围。

源码使用项目根目录 MIT LICENSE，作者为 Iris contributors。

随包示例 `assets/renderjobs/10_mouse_shader_sequence.renderjob` 使用 320×180、24 FPS、Seed 7、3 帧预热，输出第 3～8 帧，鼠标固定零输入。安装目录内可运行：

```powershell
./Iris.exe raster-sequence assets/renderjobs/10_mouse_shader_sequence.renderjob
```

输出位于安装目录的 `.local/render-jobs/10_mouse_shader_sequence/`；重跑时用 `--output` 指定新的帧路径，已有输出会拒绝覆盖。
