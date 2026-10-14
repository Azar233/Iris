# NPR 材质着色扩展

`iris.npr` 是静态、可选的 OpenGL 材质 Uniform 适配插件，依赖 `opengl.material-shading.v1`。通过现有 RenderPlugin 注册、配置和帧入口运行；帧只借用后端已经绑定的 Shader，写入既有 `uStylized*` Uniform，不绘制、不保留 Shader、不分配纹理/FBO或私有历史。Forward 与 Deferred 使用同一适配。后端继续拥有 Shader 程序、模型/材质、Pass、GL 状态和历史。

共享 CPU schema 有 25 个绑定参数，其中十个材质着色参数为：Band softness、Specular size/softness、Rim width/softness/intensity、Bands、Debug view、Shadow tint、Rim color。另有 Outline、Dither、Height fog、Color grading 的 15 个设置参数。全部读写 RendererSettings 原字段，Preset、Scene 保存与渲染设置参数页共用一份值。ShadingMode 由原有着色模式控件选择；没有新增 Job/Scene schema 或第二份覆盖存储。

为兼容旧 `.myscene`，CPU schema 保留有限 float/颜色及 int 的原范围，GPU 继续原有 GLSL 解释和 Bands 2..8 截断；渲染设置页使用通用 Drag 控件，插件页仅提供启停。NaN/Infinity、错类型、未知或重复参数仍拒绝整批修改。Debug view 的有效预览编号为 0..6；旧未知整数保持既有 Shader 行为。

活动原生 NPR 管线需要插件可用且启用，不能直接停用。先切换 PBR 或全屏 ShaderScene 后可停用；之后切换 NPR、加载 NPR Scene 或执行 NPR Raster Job 会明确拒绝，保留当前状态。插件实例按原生 NPR 使用情况创建/释放；显式 Rebuild 复用候选准备与发布合同。CPU PT 不执行该 OpenGL 扩展，保持原有 CPU 材质语义。

Outline、Dither、Height fog、Color grading 的执行和 GPU 资源仍属于 PostProcess，A4-N2 由 NPR 插件的 CPU 方法 applyPostProcessSettings 填写借用设置，后端提供附件并处理 G-buffer debug 屏蔽；本项没有修改 Shader 算法、透明语义、后端或材质数据。不是 DLL ABI、独立 Render Graph 或自定义材质语言。

```powershell
cmake --preset msvc
cmake --build --preset msvc --target MyRenderer
./tools/NprPluginAcceptance.ps1 -ScreenEffects -OutputDirectory .local/verification/a4n2
./tools/NprMigrationAcceptance.ps1 -Phase after
# 专项移除 NPR，保留普通原生 Raster 必需的 PostProcess。
cmake --preset plugins-off -DIRIS_ENABLE_POSTPROCESS_PLUGIN=ON
cmake --build --preset plugins-off --target MyRenderer
./tools/NprPluginAcceptance.ps1 -BuildDirectory build-stored-off -NprOff
```

迁移比对需要先在旧源码运行 `NprMigrationAcceptance.ps1 -Phase before`；不允许用当前源码重拍 before 来消除漂移。当前 Windows/NVIDIA 实测、图像和成本记录在本地阶段文档及 `.local/verification/a4n1` / `.local/verification/a4n2`。默认 plugins-off preset 仍关闭全部可选插件；上述显式覆盖仅为 NPR 的移除测试。
