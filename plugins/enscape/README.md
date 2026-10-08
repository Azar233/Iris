# Enscape 渲染插件验证

该插件迁移现有四 Pass 实现；Shader 数学、纹理生成、相机、参数、历史和 GPU 计时不变。开启 `IRIS_ENABLE_ENSCAPE_PLUGIN` 后静态链接到 Iris，旧 01/03 `.myscene` 继续通过原开关选择此能力。关闭构建选项后没有此 Target，普通 Renderer 仍可运行，请求此插件的 Scene/Job 明确拒绝。

插件 ID 为 `iris.enscape-study`，当前静态 API 为 2，需要宿主 `opengl.fullscreen.v1` 服务。注册表工厂创建具体实现，Renderer 仅持有 `iris::RenderPlugin`。GPU 资源仍在上下文线程创建/销毁，没有 DLL 热加载。`RendererSettings` 暂作旧参数兼容桥，尚不是通用插件参数协议。API v2 声明有序 Pass/历史及输出纹理合同；旧 `.myscene` 不存该 C++ API 版本，仍兼容。

第三方 Shader 仍在 `shaders/third_party/enscape_cube`，署名 Thomas / @Thomas_ensc（Enscape Cube）、Alexander Alekseev / TDM（Seascape），CC BY-NC-SA 3.0；见 [原许可](../../shaders/third_party/enscape_cube/LICENSE.md)。插件迁移不改变许可。

架构范围及后续工作见 [路线](../../ARCHITECTURE_ROADMAP.md)。本目录不代表全部云水/PBR/NPR 已插件化，不承诺任意 Shadertoy 自动导入或 Vulkan 支持。
