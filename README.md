<p align="center">
  <img src="assets/icons/iris-source.png" width="96" alt="Iris 鸢尾花图标" />
</p>

<h1 align="center">Iris</h1>

<p align="center">A C++17 real-time and offline rendering playground.</p>

<p align="center">
  <a href="#快速开始">快速开始</a> ·
  <a href="#主要能力">主要能力</a> ·
  <a href="#文档">文档</a>
</p>

Iris 是一个独立的图形学与渲染实验平台，将实时光栅化、CPU 路径追踪、场景编辑和批量渲染放在同一套工作流中。

项目源于我在西安交通大学图形学课程框架 [Dandelion](https://github.com/XJTU-Graphics/dandelion) 中的学习经历。Iris（鸢尾花）延续植物命名，也呼应视觉、成像与光谱的探索。

![Iris 最新编辑器：场景层级、实时视口、对象属性与资源浏览器](docs/media/iris-editor-scene-1600x900.png)

*1600×900 工作区：Hierarchy 显示 9 个实体，Scene 预览室内模型，Inspector 展示 Object 属性，Project 浏览内置场景。[截图复现](docs/media/README.md#iris-首页截图)*

## 主要能力

- **实时渲染**：OpenGL 3.3、PBR / IBL、Forward / Hybrid Deferred、阴影、SSAO、TAA 与 Toon 风格。
- **离线渲染**：CPU 渐进式路径追踪、BVH / BLAS-TLAS、MIS、自适应采样、AOV，以及 PNG / HDR / EXR 输出。
- **光与自然场景**：棱镜光谱、色散、玻璃与焦散，解析大气、体积云和海面。
- **场景与资产**：OBJ、DAE、glTF / GLB 导入，场景层级、材质编辑、骨骼动画及资源浏览。
- **统一工作流**：共享 Scene / Camera、Timeline、可复现的 C++ 模块、Render Job 与持久化 Render Queue。

<details>
<summary>更多预览：海面与光照实验</summary>

![Iris 海面实验：Renderer 属性与实时海面预览](docs/media/iris-editor-1600x900.png)

*1600×900 工作区，Inspector 的 Renderer 页展示材质、PBR 与光照参数；Scene 显示程序化海面、天空和黄色方块。[截图复现](docs/media/README.md#iris-首页截图)*

</details>

## 快速开始

推荐使用 Windows、Visual Studio 2022 和支持 OpenGL 3.3 的显卡；另需 CMake 3.20+、Git 和 Python 3。首次配置会下载锁定版本的依赖。

```powershell
git clone https://github.com/Azar233/Iris.git
cd Iris
python -m pip install "Jinja2>=2.7,<4.0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T host=x64
cmake --build build --config Release --parallel
.\build\Release\Iris.exe
```

启动后，通过 `File > Open bundled scene` 打开体积云、海面或光照实验场景。右键拖动旋转相机，中键平移，滚轮缩放；点击视口后可用 `W/A/S/D` 移动。

批量渲染与验证：

```powershell
.\build\Release\IrisBatch.exe render-sequence assets/renderjobs/01_cpu_reference.renderjob
ctest --test-dir build -C Release --output-on-failure
```

MinGW 构建、GPU smoke、发布打包及完整操作说明见[功能与开发指南](docs/project-guide.md)。

## 项目结构

```text
src/       编辑器、场景、实时渲染、路径追踪、光学与运行时
shaders/   GLSL 渲染与后处理
assets/    模型、场景、环境、图标与 Render Job
tests/     自动化测试
tools/     资产生成、验收、截图与性能工具
docs/      实现说明、实验记录与可复现证据
```

## 文档

| 主题 | 入口 |
| --- | --- |
| 功能、操作与开发 | [完整指南](docs/project-guide.md) · [术语字典](dictionary.md) |
| Editor 与 Runtime | [工作区](docs/editor-workspace-p1.md) · [模块](docs/module-runtime.md) · [批量渲染](docs/render-job-batch.md) |
| CPU 路径追踪 | [算法与验收](docs/reference-path-tracer.md) · [编辑器渐进预览](docs/cpu-progressive-preview.md) |
| 光学实验 | [棱镜与光谱](docs/prism-spectrum.md) · [玻璃与焦散](docs/glass4-validation.md) |
| 大气与自然场景 | [天空](docs/atmosphere-sky.md) · [体积云](docs/cloud-offline-runtime.md) · [海面](docs/enscape-ocean-study.md) |

Iris 仍在持续开发，实时与离线路径的支持范围并不完全相同；各主题文档记录了当前边界和验证结果。内部构建目标、环境变量和持久化标识保留旧名称以兼容已有项目，详见[开发兼容性说明](docs/project-guide.md#开发兼容性)。

## 许可与致谢

项目采用 [MIT License](LICENSE)。感谢 Dandelion 提供最初的图形学学习土壤，以及 GLFW、GLAD、GLM、Dear ImGui、Assimp 等开源项目；第三方代码和资产的许可分别见[依赖声明](THIRD_PARTY_NOTICES.md)与[资产来源](ASSET_LICENSES.md)。
