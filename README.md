<p align="center">
  <img src="assets/icons/iris-source.png" width="96" alt="Iris 鸢尾花图标" />
</p>

<h1 align="center">Iris</h1>

<p align="center">A C++17 real-time and offline rendering playground.</p>

<p align="center">
  <a href="#快速开始">快速开始</a> ·
  <a href="#主要能力">主要能力</a> ·
  <a href="#构建与测试">构建与测试</a>
</p>

Iris 是一个独立的图形学与渲染实验平台，将实时光栅化、CPU 路径追踪、场景编辑和批量渲染放在同一套工作流中。

项目源于我在西安交通大学图形学课程框架 [Dandelion](https://github.com/XJTU-Graphics/dandelion) 中的学习经历。Iris（鸢尾花）延续植物命名，也呼应视觉、成像与光谱的探索。

![Iris 最新编辑器：场景层级、实时视口、对象属性与资源浏览器](assets/readme/iris-editor-scene-1600x900.png)

*1600×900 工作区：Hierarchy 显示 9 个实体，Scene 预览室内模型，Inspector 展示 Object 属性，Project 浏览内置场景。[截图复现](assets/readme/README.md)*

## 主要能力

- **实时渲染**：OpenGL 3.3、PBR / IBL、Forward / Hybrid Deferred、阴影、SSAO、TAA 与 Toon 风格。
- **离线渲染**：CPU 渐进式路径追踪、BVH / BLAS-TLAS、MIS、自适应采样、AOV，以及 PNG / HDR / EXR 输出。
- **光与自然场景**：棱镜光谱、色散、玻璃与焦散，解析大气、体积云和海面。
- **场景与资产**：OBJ、DAE、glTF / GLB 导入，场景层级、材质编辑、骨骼动画及资源浏览。
- **统一工作流**：共享 Scene / Camera、Timeline、可复现的 C++ 模块、Render Job 与持久化 Render Queue。

<details>
<summary>更多预览：海面与光照实验</summary>

![Iris 海面实验：Renderer 属性与实时海面预览](assets/readme/iris-editor-1600x900.png)

*1600×900 工作区，Inspector 的 Renderer 页展示材质、PBR 与光照参数；Scene 显示程序化海面、天空和黄色方块。[截图复现](assets/readme/README.md)*

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

## 构建与测试

MinGW 用户可将配置命令改为 `cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`，然后运行 `cmake --build build-mingw --parallel`。

以下目标分别验证真实 OpenGL 启动、运行固定图回归和生成可独立运行的 ZIP：

```powershell
cmake --build build --config Release --target gpu-smoke
cmake --build build --config Release --target renderer-regression-suite
cmake --build build --config Release --target package
```

## 项目结构

```text
src/       编辑器、场景、实时渲染、路径追踪、光学与运行时
shaders/   GLSL 渲染与后处理
assets/    模型、场景、环境、图标与 Render Job
tests/     自动化测试
tools/     资产生成、验收、截图与性能工具
```

Iris 仍在持续开发，实时与离线路径的支持范围并不完全相同。内部 CMake target（`MyRenderer`、`MyRendererBatch`）、`MYRENDERER_*` 环境变量和持久化标识保留旧名称，以兼容已有项目。

## 许可与致谢

项目采用 [MIT License](LICENSE)。感谢 Dandelion 提供最初的图形学学习土壤，以及 GLFW、GLAD、GLM、Dear ImGui、Assimp 等开源项目；第三方代码和资产的许可分别见[依赖声明](THIRD_PARTY_NOTICES.md)与[资产来源](ASSET_LICENSES.md)。
