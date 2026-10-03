# 开放海域七方块场景

`assets/scenes/03_enscape_ocean_study.myscene` 是一个独立的开放海域视觉对照场景。构图参考 [Enscape Cube](https://www.shadertoy.com/view/4dSBDt)：低视角海平线、局部多云天空、深蓝海面，以及右侧部分入水的黄橙色方块组。原有 `02_ocean_weather_hero.myscene` 保持不变。

用户提供的 Shadertoy 文件包含主画面的多尺度海面高度场与法线、天空与海面反射、方块水下折射和接触泡沫，以及独立的 Bloom、TAA 和后处理 Pass。该源文件注明 CC BY-NC-SA 3.0，并引用了 Seascape。此场景只用仓库已有的网格海面、Kloofendal 局部多云 HDRI、七个 `cube.obj` 实体和内置后处理重建构图与参数；没有复制原 GLSL 或将其加入 MIT 代码库。

相机高度约 4.36 世界单位，海平线在画面上部，七块方块由两层 2×2 排列组成，左后方上层空出一块。每块实体可单独选择、移动和改色。海面启用多方向几何波、近景网格聚焦、较强的微法线细波和高粗糙度反射；天空盒与海面反射共用环境 HDRI。场景以 `1.25 s` 为保存的动画时间。

![七方块开放海域场景](media/p1a-enscape-ocean-study.png)

在 MSVC Release、OpenGL 3.3、4×MSAA 下，以 `920×517` 固定机位验证 `1.25 s` 和 `4.0 s` 两个时刻，七块方块均能加载且保持部分入水。场景文档测试检查内置场景数量、模型资源和七方块配置；完整 CTest 为 `26/26`。现有方块没有浮力动画，海面仍有可见的程序化反射环纹；HDRI 云层不会随时间变化，也没有实现原作的云体积和云影反射。因此该场景是可编辑的构图与材质研究场景，不是 Shadertoy 逐像素移植。

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='920'
$env:MYRENDERER_RENDER_HEIGHT='517'
$env:MYRENDERER_ANIMATION_TIME='1.25'
$env:MYRENDERER_HIDE_SELECTION_OUTLINE='1'
$env:MYRENDERER_SCREENSHOT='docs/media/p1a-enscape-ocean-study.png'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/03_enscape_ocean_study.myscene
```
