# Enscape Cube GLSL 海面研究场景

`assets/scenes/03_enscape_ocean_study.myscene` 现在直接运行用户提供的 [Enscape Cube](https://www.shadertoy.com/view/4dSBDt) 四段 GLSL：Buffer A 生成云、海面和方块；Buffer B 做泛光与 ACES；Buffer C 做时域抗锯齿；Image 做最终色散与暗角。该模式只在本场景启用，可在 Inspector 的 **Lighting & environment → Enscape Cube GLSL study** 切换。其他 `.myscene` 沿用原渲染管线，`02_ocean_weather_hero.myscene` 未修改。

四段源码及 OpenGL 3.3 包装放在 `shaders/third_party/enscape_cube/`。源文件注明 **CC BY-NC-SA 3.0**，并署名 Thomas / @Thomas_ensc；海面部分引用 Alexander Alekseev（TDM）的 Seascape。此目录独立于项目 MIT 许可，详情见该目录的 `LICENSE.md` 和根目录 `THIRD_PARTY_NOTICES.md`。原 Shadertoy 的纹理通道资源没有随文本提供，运行时生成固定种子的 256² 天气纹理和 32³ 噪声纹理，因此不会逐像素等同原作。

本次只做视觉复刻：方块是 Buffer A 内的 SDF，与编辑器实体/物理系统没有连接。为避免暗示已实现浮力，移除了原着色器按波浪调整方块姿态的代码，方块保持固定倾角和高度。海面、折射和接触效果仍按原 GLSL 绘制。另将 TAA 邻域采样限制在图像边界内，并在画面尺寸、场景或时间跳变时清除历史。该模式使用自己的时域抗锯齿，渲染目标按 1× 分配，不再为未使用的 MSAA 缓冲付费。

![GLSL 海面研究场景](media/p1a-enscape-ocean-study.png)

图示在 NVIDIA RTX 4060 Laptop、OpenGL 3.3、`920×517`、保存时间 `1.25 s`、TAA 预热 4 帧后采集：

```powershell
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_RENDER_WIDTH='920'
$env:MYRENDERER_RENDER_HEIGHT='517'
$env:MYRENDERER_ANIMATION_TIME='1.25'
$env:MYRENDERER_SCREENSHOT='docs/media/p1a-enscape-ocean-study.png'
$env:MYRENDERER_SCREENSHOT_WARMUP='4'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/03_enscape_ocean_study.myscene
```

该 GLSL 场景目前使用着色器内置相机和固定方块，Inspector 的常规海面、天空盒、SSAO 等设置不作用于此模式。后续如需保留这些编辑能力，需要将着色器参数和镜头输入映射到共享场景模型。由于 Buffer A 逐像素执行云层积分与海面高度场追踪，实时成本明显高于原网格海面路径；本阶段以参考效果验证为先。
