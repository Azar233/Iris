# Iris 首页截图

两张图片来自 revision `0d74ca4` 的真实 Iris 编辑器，使用 `build-ci-msvc` Release、NVIDIA GeForce RTX 4060 Laptop GPU、OpenGL 3.3 与驱动 591.44，于 2026-10-04 捕获。窗口尺寸为 1600×900。

- `iris-editor-scene-1600x900.png`：Hierarchy 的 9 个实体、Scene 室内模型、Object Inspector 与 Project 资源分类。
- `iris-editor-1600x900.png`：程序化海面、天空与黄色方块，Renderer Inspector 的材质、PBR 与光照参数。程序化对象不在实体层级中显示。

在项目根目录重拍：

```powershell
cmake -E env MYRENDERER_SMOKE_TEST=1 MYRENDERER_EDITOR_WINDOW_WIDTH=1600 MYRENDERER_EDITOR_WINDOW_HEIGHT=900 MYRENDERER_EDITOR_SCREENSHOT_WARMUP=2 MYRENDERER_EDITOR_SCREENSHOT_TAB=object MYRENDERER_EDITOR_SCREENSHOT=assets/readme/iris-editor-scene-1600x900.png build-ci-msvc/Release/Iris.exe assets/scenes/fixtures/13_polyhaven_studio_lounge.myscene

cmake -E env MYRENDERER_SMOKE_TEST=1 MYRENDERER_EDITOR_WINDOW_WIDTH=1600 MYRENDERER_EDITOR_WINDOW_HEIGHT=900 MYRENDERER_EDITOR_SCREENSHOT_WARMUP=2 MYRENDERER_EDITOR_SCREENSHOT_TAB=renderer MYRENDERER_EDITOR_SCREENSHOT=assets/readme/iris-editor-1600x900.png build-ci-msvc/Release/Iris.exe assets/scenes/03_enscape_ocean_study.myscene
```

截图中的 FPS 仅为当时界面状态，不作为性能结论。
