# Iris 项目目录与验证入口

源码和测试输入保留在 Git 中；机器相关缓存、编译输出、偏好设置和生成证据分别管理。目录名 `MyRenderer`、既有 CMake target、`MYRENDERER_*` 验收入口及旧 Scene/Job 格式是兼容接口，程序和新产物使用 Iris 名称。

| 位置 | 用途与保留策略 |
| --- | --- |
| `src/`、`plugins/`、`shaders/`、`assets/` | 当前核心实现、插件、Shader、示例与许可；不按文件名猜测删除资产 |
| `tests/` | 单元测试、契约与版本化回归基线 |
| `cmake/`、`tools/`、`.github/` | 配置、测试领域索引、资源同步和 CI |
| `build-ci-msvc/`、`build-mingw/` | 当前 MSVC/GCC Release 缓存；绝对路径绑定，保留可避免冷启动编译 |
| `build-stored-off/` | 可选插件 OFF 专项缓存，相关变更才启用；实际开关以 CMakeCache 为准 |
| `.cache/` | 可重建的本地依赖/工具缓存 |
| `.local/verification/` | 每个编译器/领域/级别的最新计划与日志，同一入口覆盖旧临时日志 |
| `.local/evidence/` | 已归档截图、报告和可编辑素材；旧路径映射在 `.local/maintenance/cleanup-manifest.json` |
| `.local/captures/`、`.local/benchmarks/` | 新的默认截图和手动 benchmark 输出 |
| `.local/install/Iris/` | 当前安装目录，按发布或资源路径任务刷新 |
| `.local/tools/` | 本地 GPU Capture 等工具 |
| `docs/`、`.agents/`、`AGENTS.md`、`todolist.md` | 本地说明、协作规范和路线图，保持 Git 忽略；历史证据不等于当前验收 |

Windows 偏好设置位于 `%LOCALAPPDATA%/Iris/`：`editor.ini`、`language`、`recent-scene`、`render-queue.json`。旧工作目录的 `MyRenderer.editor.v2.ini`、language/recent-scene，以及旧全局队列会在新文件不存在时复制迁移，现有 Iris 文件优先。可用 `IRIS_STATE_DIRECTORY` 隔离实验/测试；队列优先使用 `IRIS_RENDER_QUEUE_STATE`，旧 `MYRENDERER_RENDER_QUEUE_STATE` 继续兼容。自动 smoke/benchmark 不改最近场景，回归窗口不保存交互布局。

## 选择最小验证范围

先指定变更领域，再选择级别。默认 `Focused` 只构建 CMake 声明的测试拥有目标并执行对应 CTest 标签。`Integration` 执行一次全量 CPU；`Release` 加产品构建和打包。GPU 目标显式传入，逐个串行执行；不会自动插入十套图像、性能 sweep 或打包。首次配置使用锁定依赖，重复配置的 Build ID 根据源码/编译器/选项/配置保持稳定，真实源变化会失效。

```powershell
cmake --preset msvc
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area shader-scene -PlanOnly
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area shader-scene
powershell -NoProfile -File tools/Verify-Iris.ps1 -Compiler gcc -Area scene
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area plugins -Compiler plugins-off
powershell -NoProfile -File tools/Verify-Iris.ps1 -Level Integration
powershell -NoProfile -File tools/Verify-Iris.ps1 -Area shader-scene -GpuTargets shader-scene-gpu-acceptance
powershell -NoProfile -File tools/Verify-Iris.ps1 -Level Release
cmake --install build-ci-msvc --config Release --component Runtime --prefix .local/install/Iris
```

领域：`core`、`state`、`scene`、`assets`、`jobs`、`plugins`、`shader-scene`、`path-tracing`、`optics`。共享 Scene/Job/参数/API 变化在提交前跑一次全量 CPU，并检查另一编译器；渲染行为变化仍需要真实 GPU 与受影响图像，广泛集成/阶段收口才扩展完整图像套件。GPU 时间和性能对照单独执行，避免测试/构建抢占；发布必须检查独立目录启动、Job 与许可。

同一最终源码已经通过的检查只在新增相关变更、失败或未解决疑点时重跑。纯文档不启动编译和 GPU；UI 偏好路径或外观变化无需重复完整云/玻璃/PT 图像套件。跨构建目录的 CPU CTest 串行，避免共享临时夹具冲突。

发布 ZIP 使用 `cpack -C Release --config build-ci-msvc/CPackConfig.cmake -B .local/packages`，避免在根目录留下 ZIP 和 `_CPack_Packages`。安装时指定 `Runtime`，避免引入依赖库的开发头文件和静态库。

普通开发分支 CI 保留构建和完整 CPU 检查，不再每次生成发布 ZIP。`v*` 标签或手动工作流的 `package=true` 执行 CPack/上传；手动入口依赖工作流进入默认分支。并发新提交取消同一分支的旧 CI。GPU 检查仍在本地完成，CI CPU 通过不代表 GPU 通过。

A4-N1 的 NPR 移除专项使用 cmake --preset plugins-off -DIRIS_ENABLE_POSTPROCESS_PLUGIN=ON，保留普通原生 Raster 依赖的 PostProcess，关闭 Enscape/NPR；默认 preset 仍关闭全部可选插件。复用当前 build-stored-off 前核对这些实际开关，不能把该专项称作全插件 OFF。标准安装/ZIP 仍是 A3-M2，A4-N1 开发程序位于 build-ci-msvc/Release/Iris.exe；本切片未打包。
