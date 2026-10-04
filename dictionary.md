# Iris 技术术语字典

本文档记录 Iris 已使用、正在实现和路线图中即将使用的图形学与工程术语。解释优先服务于理解本项目，不追求替代完整教材。

最后更新：2026-09-14

## 阅读与维护约定

状态含义：

- `已实现`：代码中已有可运行路径并完成过构建或运行验证。
- `基础已就绪`：底层资源或 Pass 边界已经存在，但最终视觉功能尚未完成。
- `部分实现`：已有简化版本，仍缺标准完整性或质量改进。
- `计划`：已进入 `todolist.md`，尚未实现。
- `概念`：用于帮助理解设计，不代表项目一定会实现。

后续维护规则：每次引入新的渲染技术、工具或架构概念时，同步增加或更新对应词条；只有通过相应验证后才把状态改为“已实现”。

## 1. 渲染器与程序结构

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Renderer（渲染器） | 接收场景、相机和材质数据，并组织 GPU 把它们画成图像的系统。 | `src/render/Renderer.*` 负责状态设置、Pass 顺序、Shader 参数和离屏绘制。 | 已实现 |
| Rendering Pipeline（渲染管线） | 数据从模型进入 GPU，经过顶点处理、光栅化、像素着色，最终变成屏幕图像的完整流程。 | 当前主流程为 Shadow、Opaque HDR、Forward Transparent/Refractive、Bloom/Tone Map。 | 已实现 |
| Render Pass（渲染阶段） | 为完成一种明确任务而进行的一组绘制，例如阴影、主场景或后处理。 | 通过 `RenderPassSequence` 顺序执行各阶段。 | 已实现 |
| Multi-pass Rendering（多 Pass 渲染） | 同一帧分多次渲染，每次产生下一阶段需要的结果。 | 阴影贴图、HDR 主场景、Bloom 和最终合成属于不同 Pass。 | 已实现 |
| Render Graph（渲染图） | 用“资源依赖关系”自动决定 Pass 顺序、资源生命周期和同步的系统。 | 当前只有轻量顺序编排，不是通用 Render Graph。 | 计划 |
| Forward Rendering（前向渲染） | 绘制物体时直接计算光照并输出最终颜色。 | 当前不透明 PBR 路径使用前向渲染；未来透明玻璃也会使用 Forward Refractive Pass。 | 已实现 |
| Deferred Rendering（延迟渲染） | 先把几何信息写入 G-Buffer，再统一计算光照，适合大量动态灯光。 | 图形程序路线中的候选功能；透明玻璃通常仍需单独走 Forward Pass。 | 计划 |
| Application Layer（应用层） | 管理窗口、输入、UI、主循环等外围工作，不应承载所有底层渲染细节。 | `src/app/Application.*` 目前仍较大，后续会继续拆分。 | 部分实现 |
| Asset Pipeline（资产管线） | 模型和纹理从文件进入 CPU 数据、校验、上传 GPU 并用于渲染的全过程。 | OBJ/DAE/glTF/GLB 最终转换成统一 `ModelData`，再创建 `GpuModel`。 | 已实现 |
| Format-independent Data（格式无关数据） | 渲染层只认识统一内部结构，不关心原文件是哪种格式。 | `ModelData` 隔离 tinyobjloader、Assimp 与渲染代码。 | 已实现 |
| RAII | 用 C++ 对象生命周期自动管理 GPU/系统资源，构造时获取，析构时释放。 | Shader、Texture、Mesh、Framebuffer 等资源通过类封装释放。 | 已实现 |
| CPU / GPU Boundary（CPU/GPU 边界） | 区分 CPU 负责的导入、组织工作与 GPU 负责的并行绘制工作。 | 模型后台导入在 CPU 完成，OpenGL 资源上传在拥有上下文的主线程完成。 | 已实现 |
| DCC（Digital Content Creation） | Blender、Maya、3ds Max、Substance 等美术制作软件的统称。 | TA 路线计划增加 Blender 导出与校验工具。 | 计划 |
| Technical Artist（TA，技术美术） | 连接美术与程序，负责材质、工具、资产规范、性能预算和生产流程。 | 本项目可通过资产审阅、材质调试和玻璃效果工具体现 TA 能力。 | 概念 |

## 2. GPU 光栅化基础

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Rasterization（光栅化） | 把三角形覆盖的区域转换成一个个待着色的像素/片元。 | OpenGL GPU 实时主渲染方式；CPU Reference Path Tracer 作为独立对照路径。 | 已实现 |
| Vertex Shader（顶点着色器） | GPU 对每个顶点执行的程序，主要完成坐标变换并输出插值数据。 | `basic.vert` 计算世界位置、法线、切线、MVP 和阴影坐标。 | 已实现 |
| Fragment Shader（片元着色器） | GPU 对每个可见片元执行的程序，计算材质、光照和最终颜色。 | `basic.frag` 实现 Blinn-Phong/PBR、IBL、法线贴图和阴影。 | 已实现 |
| GLSL | OpenGL 使用的 Shader 编程语言。 | `shaders/` 中的 `.vert`、`.frag` 文件使用 GLSL 330 Core。 | 已实现 |
| Uniform | CPU 为一次或一批 Draw Call 设置、在 Shader 中只读的参数。 | 用于传递矩阵、灯光、相机、材质开关与强度。 | 已实现 |
| Attribute（顶点属性） | 每个顶点携带的数据，例如位置、法线、UV、切线。 | 当前顶点布局包含 Position、Normal、UV0、Tangent。 | 已实现 |
| Interpolation（插值） | GPU 在三角形内部自动根据三个顶点平滑计算片元数据。 | 世界位置、法线、UV 等从 Vertex Shader 插值到 Fragment Shader。 | 已实现 |
| VAO | 记录顶点属性如何从 Buffer 中读取的 OpenGL 对象。 | `Mesh` 用 VAO 保存顶点布局和相关 Buffer 绑定。 | 已实现 |
| VBO | 存放顶点数据的 GPU Buffer。 | 保存 Position、Normal、UV、Tangent 等顶点数据。 | 已实现 |
| EBO / Index Buffer | 存放顶点索引，用复用顶点的方式组成三角形。 | `Mesh` 通过 `glDrawElements` 使用索引绘制。 | 已实现 |
| Draw Call | CPU 向 GPU 提交一次绘制命令。数量太多会增加 CPU/驱动开销。 | 当前按 Mesh 的 Submesh/Material 范围提交并统计。 | 已实现 |
| MVP（Model/View/Projection） | Model 放置物体，View 表示相机，Projection 把 3D 投影到屏幕。 | `basic.vert` 用三矩阵计算最终裁剪空间位置。 | 已实现 |
| Coordinate Space（坐标空间） | 同一点可以用模型、世界、观察、裁剪、屏幕等不同坐标描述。 | 光照在世界空间计算；阴影还会转换到 Light Space。 | 已实现 |
| Viewport（视口） | 最终渲染结果在目标纹理或窗口中的像素区域。 | 随 ImGui 渲染面板大小调整离屏 RenderTarget。 | 已实现 |
| Depth Test（深度测试） | 比较片元与已保存深度，只保留更靠近相机的表面。 | 解决模型前后遮挡，也会供未来折射深度追踪使用。 | 已实现 |
| Depth Write（深度写入） | 决定通过测试的片元是否更新深度。透明物体通常测试深度但不写入。 | 不透明/MASK 写深度；BLEND 保持深度测试但关闭深度写入。 | 已实现 |
| Stencil Buffer（模板缓冲） | 每个像素保存一个小整数，可用作局部遮罩。 | 当前与 Depth 一起使用 `DEPTH24_STENCIL8`，尚未用于特效遮罩。 | 基础已就绪 |
| Back-face Culling（背面剔除） | 不绘制朝向背面的三角形，以减少无用工作。 | Inspector 中可开关；阴影 Pass 会临时剔除正面以减轻阴影痤疮。 | 已实现 |
| Wireframe（线框模式） | 只显示三角形边线，用于查看拓扑与细分密度。 | Inspector 可切换。 | 已实现 |

## 3. Framebuffer、RenderTarget 与颜色缓冲

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Framebuffer / FBO | GPU 渲染的目标集合，可以包含颜色、深度、模板等附件。 | 主场景、阴影、Bloom、最终 LDR 都使用离屏 FBO。 | 已实现 |
| RenderTarget（渲染目标） | 对 Framebuffer 和附件纹理的工程封装。 | `RenderTarget` 管理 Opaque HDR、最终 HDR、Depth/Stencil 和最终 LDR。 | 已实现 |
| Attachment（附件） | 挂在 FBO 上、用于接收颜色或深度结果的纹理/Renderbuffer。 | 当前包含颜色纹理、Depth/Stencil 纹理和 MSAA Renderbuffer。 | 已实现 |
| Off-screen Rendering（离屏渲染） | 不直接画到窗口，而是先画到纹理，便于 UI 展示与后处理。 | ImGui Viewport 显示最终 LDR Texture。 | 已实现 |
| HDR（High Dynamic Range） | 使用浮点颜色保存大于 1 的高亮，不会过早把亮度截断。 | 主场景使用 `RGBA16F`，为 Bloom、曝光和高亮焦散保留能量。 | 已实现 |
| LDR（Low Dynamic Range） | 常规 0～1 范围的显示颜色。 | Tone Mapping 后写入 `RGBA8` 最终纹理。 | 已实现 |
| MSAA | 在同一像素内采多个几何覆盖样本，主要改善三角形边缘锯齿。 | 支持 1x/4x；不等同于 TAA，也不能单独解决 Shader 闪烁。 | 已实现 |
| Resolve | 把多采样颜色/深度合并成普通单采样纹理，供后续 Pass 读取。 | 4x 模式现已 Resolve Color、Depth 和 Stencil。 | 已实现 |
| Sampleable Depth（可采样深度） | 深度不只用于遮挡，还能像纹理一样被后续 Shader 读取。 | Glass-0 已建立 `GL_DEPTH24_STENCIL8` 深度纹理。 | 已实现 |
| Opaque Color Texture | 只包含不透明场景的 HDR 颜色，是玻璃折射看到的背景。 | Glass-0 已与最终 HDR 输出分离。 | 已实现 |
| Scene Color Texture | 包含不透明场景以及后续玻璃等效果的最终 HDR 场景颜色。 | 后处理从它提取 Bloom 并执行 Tone Mapping。 | 已实现 |
| Framebuffer Feedback | 一个 Pass 同时读取和写入同一纹理，会产生未定义或不可预测结果。 | 折射 Pass 使用独立 Opaque/HDR Color，并把深度测试复制到 Renderbuffer；可采样 Opaque Depth 不再同时作为当前附件。 | 已解决 |
| Renderbuffer | 只能作为渲染附件、通常不能直接在 Shader 里采样的存储对象。 | MSAA Color 和 MSAA Depth/Stencil 当前使用 Renderbuffer。 | 已实现 |

## 4. 模型、网格与资产导入

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Mesh（网格） | 由顶点和三角形组成的几何对象。 | `MeshData` 保存 CPU 数据，`Mesh` 保存 VAO/VBO/EBO。 | 已实现 |
| Submesh（子网格） | 同一 Mesh 中使用不同材质或索引区间的一部分。 | 每个 Submesh 对应材质编号和一个 Draw Call。 | 已实现 |
| Render Item（渲染项） | 把一个 GPU 模型、世界变换、颜色和显隐/阴影标记组合成一次场景提交。 | `Renderer` 每帧接收多个 `RenderItem`，因此主体、地面和辅助实例可以共用同一套 Pass。 | 已实现 |
| Scene Draw List（场景绘制列表） | 渲染前整理出的待绘制命令集合，便于统一分类和排序。 | BLEND 子网格从所有可见 Render Item 汇总后再做全场景排序。 | 已实现 |
| Shadow Receiver（阴影接收器） | 会显示其他物体投影的表面，不一定需要自己投射阴影。 | 程序化地面参与 PBR 主 Pass、采样 Shadow Map，但设置为不写入阴影图。 | 已实现 |
| Vertex（顶点） | 构成三角形的点及其附加属性。 | 当前包含位置、法线、UV0 和带手性的切线。 | 已实现 |
| Index（索引） | 指向顶点数组的编号，用于复用顶点组成三角形。 | 由 OBJ/Assimp 导入并上传 EBO。 | 已实现 |
| Triangulation（三角化） | 把四边形或多边形拆成 GPU 易处理的三角形。 | tinyobjloader 与 Assimp 导入时执行。 | 已实现 |
| AABB | 与坐标轴平行的包围盒，用最小/最大 XYZ 包住模型。 | 用于模型归一化、自动取景、统计和未来裁剪。 | 已实现 |
| Normal（法线） | 表示表面朝向的向量，决定光照强弱与反射方向。 | 缺失法线时导入器会生成平滑法线。 | 已实现 |
| UV / Texture Coordinate | 把模型表面位置映射到二维纹理坐标。 | 当前支持 UV0。 | 已实现 |
| Tangent Space（切线空间） | 以表面切线、双切线、法线构成的局部坐标系。 | 用于把法线贴图中的方向转换到世界空间。 | 已实现 |
| Tangent Handedness（切线手性） | 用 `+1/-1` 表示双切线方向，处理镜像 UV。 | 存储在 `Vertex.tangent.w`。 | 已实现 |
| Degenerate UV（退化 UV） | 三角形的 UV 面积接近零，无法建立可靠切线空间。 | 导入时记录诊断并回退到几何法线。 | 已实现 |
| OBJ | 传统文本模型格式，适合简单几何与回归测试。 | 使用 tinyobjloader 独立导入。 | 已实现 |
| DAE / COLLADA | XML 场景交换格式，可包含节点、Mesh、材质。 | 通过 Assimp 导入，主要兼容旧 Dandelion 资产。 | 已实现 |
| glTF 2.0 | 面向实时运行时传输的标准 3D 格式。 | 当前主要材质与 PBR 交换格式。 | 部分实现 |
| GLB | 把 glTF JSON、Buffer 和图像打包在一个二进制文件中。 | 通过 Assimp 支持内嵌纹理。 | 已实现 |
| Assimp | 统一读取多种模型格式的第三方资产导入库。 | 只开启 COLLADA 与 glTF Importer，第三方类型不进入渲染层。 | 已实现 |
| Data URI | 把二进制图片用编码文本直接嵌入 glTF JSON。 | `textured_triangle.gltf` 用作固定回归资产。 | 已实现 |
| Embedded Texture（内嵌纹理） | 图像数据存放在模型文件内部，而不是外部图片路径。 | 支持 glTF Data URI 与 GLB/Assimp 内嵌图像。 | 已实现 |
| Texture Cache（纹理缓存） | 相同纹理只解码和上传一次，多个材质共享 GPU 对象。 | `TextureCache` 按 Cache Key 复用纹理。 | 已实现 |
| Fallback Texture（回退纹理） | 纹理缺失或损坏时显示的替代纹理，避免崩溃或黑屏。 | 无纹理使用白色；加载失败使用洋红棋盘。 | 已实现 |

## 5. 纹理与颜色空间

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Texture Sampling（纹理采样） | Shader 根据 UV 从纹理读取颜色或数据。 | 基础色、法线、金属度/粗糙度、阴影等都通过采样获得。 | 已实现 |
| CPU Texture Cache | 在离线路径追踪开始前把快照中的纹理解码为线性可查询 texel，避免每条光线路径重复读文件或解码。 | `SceneTextures` 按 Asset 保存 Base Color、Metallic-Roughness、Normal 与 Thickness 的只读 CPU 副本。 | 已实现 |
| Bilinear Filtering（双线性过滤） | 取 UV 周围四个 texel，并按小数位置插值，避免最近邻采样的方块跳变。 | CPU Reference Path Tracer 采用 Repeat 与 texel-center 双线性，并让 sRGB texel 在线性空间参与过滤。 | 已实现（基础层） |
| Sampler | 定义纹理过滤与超出 UV 范围时如何重复/截断的规则。 | 当前 OpenGL Texture 有固定参数；完整 glTF Sampler 语义待补。 | 部分实现 |
| Mip / Mipmap | 同一纹理的多级缩小版本，远处或模糊采样时减少闪烁并提高缓存效率。 | 环境 Cubemap 与 Opaque HDR Color 都有完整 Mip；粗糙玻璃按 Roughness 选择更模糊层级。 | 已实现 |
| sRGB | 面向显示和图片存储的非线性颜色编码。 | 基础色纹理以 sRGB 格式上传并由 GPU 解码。 | 已实现 |
| Linear Color Space（线性颜色空间） | 数值与真实光能近似成正比，适合光照和混合计算。 | 材质与灯光在线性空间计算。 | 已实现 |
| Gamma Correction（Gamma 校正） | 在线性计算与显示编码之间进行转换。 | 最终输出只执行一次 Linear-to-sRGB，避免重复 Gamma。 | 已实现 |
| Data Texture（数据纹理） | 存储法线、粗糙度等数值，而不是可直接观看的颜色。 | 法线与 Metallic-Roughness 纹理保持线性采样。 | 已实现 |
| Normal Map（法线贴图） | 用纹理改变表面微小朝向，不增加真实几何。 | 使用 TBN 切线空间矩阵影响光照。 | 已实现 |
| Base Color / Albedo | 材质在不包含光照时的基础颜色。 | 来自 Factor × Texture × Inspector Tint。 | 已实现 |

## 6. 光照、BRDF 与 PBR

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Blinn-Phong | 经典经验光照模型，用漫反射和高光近似表面明暗。 | 可作为非 PBR 对照路径。 | 已实现 |
| PBR（Physically Based Rendering） | 使用接近物理规律、参数可跨环境复用的材质与光照方法。 | 当前采用 glTF Metallic-Roughness 工作流。 | 已实现 |
| NPR（Non-Photorealistic Rendering） | 不追求照片级物理还原，而是有意识地控制明暗、轮廓和色彩语言。 | SR-P2 复用现有 Scene、灯光与后处理，提供独立 Stylized / Toon Render Mode。 | 已实现（表面光照） |
| Toon Ramp / Lighting Bands | 把连续光照量化成少量明暗层级，形成动画或插画式块面。 | 主光与局部灯的 `N·L` 可量化为 2～8 档，并以 Band Softness 控制过渡。 | 已实现 |
| Rim Light（边缘光） | 根据表面与视线的夹角强化物体轮廓附近的亮边。 | Stylized 模式可调颜色、宽度、软度与强度，背光侧权重更高。 | 已实现 |
| Shadow Tint（阴影染色） | 用指定颜色替代纯黑阴影，让暗部保持统一的美术色调。 | Toon 方向光把 Shadow Map 可见度与场景级 Shadow Tint 混合。 | 已实现 |
| Screen-space Outline（屏幕空间描边） | 从已经渲染的深度和法线图寻找轮廓，不要求复制并膨胀原模型。 | SR-P2B 在 TAA 后按像素宽度合成；Deferred 使用 Depth/Normal，Forward 使用 Depth-only。 | 已实现 |
| BRDF | 描述光从某方向射入后，会以多少能量反射到观察方向的函数。 | Cook-Torrance 用多个项组合出 PBR 高光与漫反射。 | 已实现 |
| Cook-Torrance | 常用微表面 PBR BRDF 框架。 | `basic.frag` 组合 GGX 分布、几何遮蔽和 Fresnel。 | 已实现 |
| GGX | 描述微表面法线分布的模型，能产生较自然的长尾高光。 | 当前用于 PBR Specular Distribution。 | 已实现 |
| Metallic（金属度） | 0 通常表示绝缘体，1 表示金属；决定高光颜色和漫反射比例。 | glTF 打包纹理 B 通道与 Factor 相乘。 | 已实现 |
| Roughness（粗糙度） | 表面越粗糙，高光越宽越模糊。 | glTF 打包纹理 G 通道与 Factor 相乘。 | 已实现 |
| Fresnel Effect（菲涅耳效应） | 观察角度越贴近表面，反射通常越强。 | PBR 当前使用 Schlick 近似；玻璃反射/透射也依赖它。 | 已实现 |
| Energy Conservation（能量守恒） | 反射、透射、吸收的光能总和不应凭空超过入射能量。 | 当前不透明 PBR近似遵循；Glass 材质需继续保证。 | 部分实现 |
| IBL（Image-based Lighting） | 用环境图像提供来自四面八方的光照和反射。 | 实时路径使用预积分 Cubemap；CPU 参考路径直接采样等距柱状 HDR Environment。 | 已实现 |
| Environment Importance Sampling | 按环境 texel 的亮度与球面面积选择方向，让太阳等高亮小区域获得更多样本。 | SR-P1F 使用 Rec.709 Luminance × `sin(theta)` CDF，并输出立体角 PDF 参与 MIS。 | 已实现 |
| Infinite-area Light（无限环境光） | 位于无限远、只由方向决定亮度的光源；阴影查询没有有限终点。 | HDR Environment 作为 `SceneLights` 条目执行 NEE，无限 Shadow Ray 检查几何遮挡。 | 已实现 |
| Cubemap | 由六个方向组成的立方体纹理，适合表示远处环境。 | 用于天空盒与 PBR 环境采样。 | 已实现 |
| Skybox（天空盒） | 把环境 Cubemap 作为无限远背景显示。 | 在主场景模型之前绘制。 | 已实现 |
| Split-sum IBL | 把环境镜面反射积分拆成预过滤 Cubemap 与 BRDF LUT，运行时快速组合。 | P0 计划从当前近似 IBL 升级。 | 计划 |
| Irradiance Map | 对环境光做低频卷积，表示漫反射接收到的平均光照。 | 标准 IBL 路线的一部分。 | 计划 |
| Prefiltered Environment Map | 按不同粗糙度预先模糊环境反射。 | 当前有近似 Mip；标准生成流程待实现。 | 部分实现 |
| BRDF LUT | 二维查找表，预存环境镜面 BRDF 中与视角和粗糙度有关的积分结果。 | 标准 Split-sum IBL 路线需要。 | 计划 |

## 7. 阴影与光空间

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Shadow Map（阴影贴图） | 从光源视角记录最近深度，再判断相机看到的点是否被遮挡。 | 当前为方向光生成 2048² 深度纹理。 | 已实现 |
| Light Space（光源空间） | 以光源作为“相机”的坐标空间。 | 顶点转换到 Light View-Projection 后查询 Shadow Map。 | 已实现 |
| PCF | 对阴影贴图周围多个深度样本求平均，使边缘不那么硬。 | 当前使用 3×3 PCF。 | 已实现 |
| PCSS | 先估算遮挡物距离，再按估算出的半影宽度改变过滤范围，实现接触处锐利、远处柔和的阴影。 | 已加入后续阴影路线；需先稳定 Light Frustum、Texel Snapping 和 Bias。 | 计划 |
| Blocker Search（遮挡物搜索） | 在 Shadow Map 邻域里找出真正比接收面更靠近光源的深度样本。 | PCSS 的第一阶段，将用于估算平均遮挡深度。 | 计划 |
| Penumbra（半影） | 阴影边缘只有部分光源可见的过渡区域。 | PCSS 将根据接收面与遮挡物距离动态估算其宽度。 | 计划 |
| Shadow Acne（阴影痤疮） | 精度误差造成表面出现条纹或自阴影噪点。 | 当前通过 Bias 和阴影 Pass 剔除策略减轻。 | 部分实现 |
| Peter Panning | 阴影 Bias 过大导致阴影看起来与物体分离。 | 后续阴影调试视图需要同时平衡它与 Acne。 | 计划 |
| CSM（Cascaded Shadow Maps） | 按相机距离分多级阴影范围，让近处保持更高精度。 | P0 阴影质量路线。 | 计划 |
| Transmissive Shadow（透射阴影） | 光穿过透明物体后形成带颜色和衰减的阴影。 | RGBA16F Light-space 纹理以乘法混合累积 Beer-Lambert 透射率，再与 PCF 可见度相乘。 | 已实现 |

## 8. 后处理与显示

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Post-processing（后处理） | 场景渲染完成后，对整张图像执行的效果。 | Bloom、Tone Mapping 和 sRGB 输出由 `PostProcessor` 完成。 | 已实现 |
| Tone Mapping（色调映射） | 把 HDR 亮度压缩到普通显示器可显示范围。 | 支持 ACES 近似和关闭对照。 | 已实现 |
| ACES | 常用电影风格色调映射曲线，能较自然地压缩高亮。 | `postprocess.frag` 中实现近似公式。 | 已实现 |
| Exposure（曝光） | 在 Tone Mapping 前整体放大或缩小 HDR 亮度。 | Inspector 可调。 | 已实现 |
| Bloom | 提取高亮并模糊叠加，让强光产生泛光。 | 当前阈值提取后进行多次水平/垂直高斯模糊。 | 已实现 |
| Threshold（阈值） | 只有亮度超过指定值的像素才进入 Bloom。 | `bloomThreshold` 控制提取范围。 | 已实现 |
| Gaussian Blur（高斯模糊） | 按距离加权平均邻近像素，产生平滑模糊。 | Bloom 使用分离式水平/垂直模糊。 | 已实现 |
| Chromatic Aberration（镜头色差） | 镜头造成画面边缘 RGB 通道错位的全屏效果。 | 尚未实现；不能代替玻璃材质内部的光谱色散。 | 概念 |

## 9. 玻璃、折射与焦散

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| Opaque（不透明） | 光不能穿过，通常输出颜色并写入深度。 | glTF `OPAQUE` 与 `MASK` 材质进入不透明队列。 | 已实现 |
| Alpha Blending（透明混合） | 根据 Alpha 把前景颜色与已画背景做比例混合。 | 已支持 glTF `BLEND` 的标准 Over 合成；它本身不会产生折射。 | 已实现 |
| Transparent Sorting（透明排序） | 通常按从远到近绘制透明表面，减少混合顺序错误。 | 当前汇总所有可见 Render Item 的 BLEND 子网格，并按世界空间中心距离稳定地从远到近排序。 | 已实现 |
| Transmission（透射） | 光穿过材质继续传播，是玻璃区别于普通半透明贴纸的核心。 | 已导入 `KHR_materials_transmission`，并在 Refractive Pass 混合透射背景与介质反射。 | 基础已实现 |
| Refraction（折射） | 光进入不同介质时改变传播方向，使玻璃后的背景发生扭曲。 | Glass Shader 已按法线和 IOR 计算折射方向，并采样 Opaque Scene。 | 第一版已实现 |
| Refractive Pass（折射 Pass） | 在不透明场景完成后，专门绘制透明、玻璃、水晶等材质的阶段。 | 当前在独立 HDR 输出中绘制 Alpha Blend 与光学透射材质，并采样 Opaque Color/Depth 完成折射。 | 已实现 |
| Screen-space Refraction（屏幕空间折射） | 利用当前画面的颜色与深度近似追踪折射，只能看到屏幕中已经存在的内容。 | 第一版按折射方向偏移世界位置并采样 Opaque Color/Depth；越界或深度不匹配时回退环境图。 | 基础已实现 |
| Screen-space Ray March（屏幕空间步进） | 沿一条三维射线分多步投影到屏幕，并与画面深度比较来寻找交点。 | 折射最多使用 32 步；Refraction Scale 控制最大距离，Steps 控制精度和成本。 | 已实现 |
| Rough Refraction（粗糙折射） | 表面微小方向越杂乱，透过它看到的背景越模糊。 | 使用 Roughness 选择 Opaque HDR Mip；环境回退选择预过滤 Cubemap Mip。 | 已实现 |
| Refracted UV | 折射射线最终在 Opaque Scene Color 上采样的二维坐标。 | Glass Debug View 用 RG 显示 UV，蓝色表示命中，洋红表示回退。 | 已实现 |
| IOR（折射率） | 表示光在介质中传播速度差异的参数，决定折射弯曲和基础反射比例。 | 已支持 `KHR_materials_ior`；默认 1.5，并用于 Fresnel F0 与 Snell 折射。 | 已实现 |
| Snell's Law（斯涅尔定律） | 根据两种介质折射率和入射角计算折射方向。 | Glass Shader 通过 GLSL `refract` 按空气/介质两侧 IOR 比计算。 | 已实现 |
| Total Internal Reflection（全反射） | 从高折射率介质向外传播且角度过斜时，光不再透出而完全反射。 | `refract` 无有效方向时回退到环境反射。 | 已实现 |
| KHR_materials_volume | glTF 的体积材质扩展，为透射表面补充厚度和介质吸收参数。 | 已导入 Thickness Factor/Texture、Attenuation Color 与 Attenuation Distance；Thickness Texture 按规范读取 G 通道。 | 已实现 |
| Thickness（厚度） | 光在物体内部实际走过的距离，影响折射位移和吸收强度。 | Glass-2B 优先由每像素 Front/Back Depth 估算闭合模型几何厚度，再按折射角换算内部路径。 | 已实现（屏幕空间） |
| Thickness Texture（厚度贴图） | 为模型表面每个 UV 位置预烘焙局部厚度的线性数据纹理；glTF 把数值放在 G 通道，并与 Thickness Factor 相乘。 | 几何退出表面无效或关闭 Geometric Thickness 时作为稳定回退。 | 已实现 |
| Front/Back Surface Depth（前/后表面深度） | 从相机方向记录物体最先进入和最后离开的表面深度，两者差值可近似光穿过的几何距离。 | 两张 R32F 纹理用 `GL_MIN` / `GL_MAX` 聚合闭合玻璃的入口/退出深度。 | 已实现 |
| Closed Manifold Mesh（闭合流形网格） | 网格完整包围一个内部空间，通常每条边恰好连接两个面；渲染器因此可以区分进入和离开介质。 | `glass_volume_sphere.gltf` 使用 1,986 个顶点构成平滑闭合球；CPU 回归逐边要求恰好被两个三角形使用。 | 已实现并验证 |
| Exit Surface Normal（出射面法线） | 光在离开玻璃的位置所遇到表面的朝向；弯曲物体的出射法线通常不等于入口法线。 | Glass-2C 以 RGBA16F 保存对象最远表面法线，内部射线跨越 R32F 退出深度时读取并显示调试色。 | 已实现（屏幕空间） |
| Two-interface Refraction（双界面折射） | 光进入玻璃时弯折一次，离开玻璃时根据另一侧表面方向再次弯折。 | Glass Shader 对空气→玻璃与玻璃→空气分别应用 Snell 定律，并插值退出交点以消除步进色带。 | 已实现（可切换） |
| Object-ID-aware Depth Pairing（对象 ID 深度配对） | 用对象标识确保入口和出口来自同一个玻璃物体，避免重叠物体的深度被错误组合。 | 每个透明 `RenderItem` 在绘制前重建并立即消费自己的深度/法线缓存，R32UI ID 在 Shader 内再次校验。 | 已实现（逐对象） |
| Split-Sum IBL | 将环境镜面光预积分成按粗糙度过滤的 Cubemap 与二维 BRDF LUT，并把漫反射单独卷积，避免每像素对 HDR 环境做大量采样。 | `EnvironmentMap` 从 Poly Haven CC0 OpenEXR HDRI 生成 Diffuse Irradiance、GGX Prefiltered Specular 和 RG16F BRDF LUT。 | 已实现 |
| Beer-Lambert Law | 光在介质中传播越远，被吸收越多；不同颜色可以有不同吸收。 | Shader 使用 `attenuationColor^(pathLength/attenuationDistance)` 计算玻璃透射率。 | 已实现 |
| Attenuation（衰减/吸收） | 光穿过介质后亮度和颜色逐渐减少。 | glTF 材质的 Color / Distance 决定两组测试玻璃的青色和琥珀色体积。 | 已实现 |
| Transmittance（透射率） | 光穿过一段介质后还剩下的比例，1 表示没有损失，0 表示完全吸收。 | Glass Debug View 可直接显示 Beer-Lambert 计算出的 RGB 透射率。 | 已实现 |
| KHR_materials_dispersion | glTF 的材质色散扩展，用一个非负 `dispersion` 参数描述透射介质分色强度。 | glTF/GLB 适配器把扩展写入 `MaterialData`，GPU 材质默认使用它；Inspector 的非零 Override 可覆盖。 | 已实现 |
| Spectral Dispersion（光谱色散） | 不同波长的折射率不同，白光经过水晶后分离成彩虹。 | Glass Shader 支持 R/G/B 近似；PrismOptics 进一步对 380～700 nm 的离散波长逐条求解。 | 已实现 |
| Abbe Number（阿贝数） | 描述材料色散强弱的参数；数值越低通常色散越明显。 | 按 glTF 规范换算 `Vd = 20 / dispersion`；Inspector 会显示非零 Override 对应的 Abbe 值。 | 已实现 |
| Prism Dispersion（棱镜分光） | 白光经过棱镜的入射和出射两个界面后，不同波长沿不同方向离开。 | Prism-2 已从世界空间入射光生成连续或七色出射光路；柔边带状光束归入 Prism-3。 | 基础已实现 |
| Ray–Prism Intersection（射线—棱镜求交） | 计算一条有方向的光线最先撞到棱镜哪个面、撞击点在哪里。 | `PrismOptics` 先求入射面，再从内部方向求出射面。 | 已实现 |
| Surface Normal（表面法线） | 垂直于表面的方向，用来判断光从哪一侧入射并计算折射角。 | 每个三角形截面边生成朝外法线，分别用于空气→玻璃和玻璃→空气。 | 已实现 |
| Fresnel Transmittance（菲涅耳透射率） | 光到达介质边界后，没有被反射、继续穿过边界的能量比例。 | Prism-1 在入射/出射两个界面分别计算并相乘。 | 已实现 |
| Cauchy's Equation（柯西方程） | 用少量材料参数近似折射率随光波长的变化。 | `prismIorAtWavelength` 按 Khronos 公式从中心 IOR 与 Abbe Number 计算 380～700 nm 的 IOR。 | 已实现 |
| Spectral Sampling（光谱采样） | 把连续可见光拆成有限个波长样本分别计算，再合成为显示颜色。样本越多越平滑，但 CPU/GPU 成本也越高。 | Prism-2 提供 7/15/21/31 四档，默认 21；能量归一化避免增加样本后无故变亮。 | 已实现 |
| CIE 1931 Color Matching Functions | 描述不同波长对人眼 XYZ 三刺激值贡献的标准曲线，是把“物理波长”转换成“显示颜色”的桥梁。 | 使用 Wyman、Sloan、Shirley 的解析高斯近似，再由 XYZ 转换到线性 sRGB。 | 已实现（解析近似） |
| Wavelength-to-RGB（波长到 RGB） | 把某个纳米波长对应的人眼颜色换成显示设备的红绿蓝数值；这不是简单手写七种颜色。 | `wavelengthToLinearSrgb` 先求 CIE XYZ，再转换并裁剪到线性 sRGB，Tone Mapping 前不做 Gamma 编码。 | 已实现 |
| Spectral Energy Normalization（光谱能量归一化） | 把所有波长样本的权重重新缩放，使总和保持固定，避免采样数翻倍时亮度也翻倍。 | `tracePrismSpectrum` 把两界面 Fresnel 与波长相关 Beer-Lambert 衰减相乘后，将样本总权重归一到 1。 | 已实现 |
| Emissive Ribbon（自发光带状几何） | 用一条由三角形组成、带宽度的窄带表现光束；它能使用 Shader 做柔边，不受 OpenGL Line 宽度限制。 | `SpectralBeamMesh` 生成入射、内部和出射三组 Ribbon，`SpectralBeamRenderer` 在 HDR 中绘制。 | 已实现 |
| Camera-facing Ribbon（相机朝向带） | 根据光束方向和相机视线计算带宽方向，使窄面始终大致朝向镜头，避免侧看时消失。 | 每帧根据 Camera Position 重建轻量光束顶点；光路中心仍保持世界空间位置。 | 已实现 |
| Volumetric Scattering（体积散射） | 光被空气、雾或尘埃散射后，观察者才会从侧面看到光柱。 | Prism 第一版用 Ribbon 明确作为光路可视化；真实体积散射是可选高质量模式。 | 计划 |
| Thin-film Iridescence（薄膜虹彩） | 薄层内部多次反射产生干涉，随角度呈现彩色表面。 | 可作为参考效果的可选表面增强，不等同于体积色散。 | 计划 |
| Caustics（焦散） | 光经过反射或折射后聚集，在接收表面形成明亮花纹。 | Glass-3 同时提供可控 Projector 和几何驱动 Light-space RGB 路线。 | 已实现 |
| Caustics Projector / Decal | 把预制或程序化焦散图案投射到地面，以较低成本控制视觉效果。 | 程序化 RGB 环带写入 1024² HDR 焦散纹理，支持强度、尺度、方向、锐度与动画。 | 已实现 |
| Light-space Caustics | 从光源方向计算折射光落点并累积能量的实时焦散近似。 | 方向光下按 R/G/B IOR 计算接收平面落点并加法累积。 | 已实现 |
| Photon Splat（光子样本光斑） | 把一条光能样本落点画成一个很小的柔边光斑，大量样本重叠后形成焦散。 | Geometry Shader 为每个入射三角形生成 Light-space 小四边形，避免拉伸三角形尖刺。 | 已实现 |
| Spatial Filtering（空间滤波） | 在同一帧相邻像素间平滑信号，降低锯齿和高频闪烁。 | 焦散纹理使用横向/纵向两次可调 Gaussian Filter。 | 已实现 |
| Additive Blending（加法混合） | 把新颜色直接加到已有颜色上，适合表示光和能量叠加；重叠越多通常越亮。 | Prism Beam 与 Glass-3 Caustics 都用 `GL_ONE + GL_ONE` 写入 RGBA16F。 | 已实现 |
| Edge Softness（边缘柔度） | 控制光束从亮核心到透明边缘的过渡宽度，值越大过渡越柔和。 | Beam Fragment Shader 根据 Ribbon 横向坐标执行 `smoothstep`，Inspector 可实时调节。 | 已实现 |
| White Point（白点） | 定义“什么颜色应被看作白色”；改变它会让同一束白光偏暖或偏冷。 | Prism-4 以 Kelvin 参数生成线性 sRGB 调制色，同时作用于入射、内部和出射光束。 | 已实现（显示近似） |
| Color Temperature / Kelvin（色温 / 开尔文） | 用温度近似描述光源颜色；较低数值偏暖黄，较高数值偏冷蓝。它描述颜色倾向，不等于光源实际温度测量。 | Inspector 的 White Point 范围为 2000～12000 K，自动化也可由环境变量覆盖。 | 已实现 |
| Optical Preset（光学预设） | 一组可复用参数快照；切换材料风格时改变 IOR、色散、采样和吸收等数据，而不是复制一份 Shader。 | Prism-4 提供 Crown Glass、Water-like、Diamond-like、Exaggerated Cover 四组。 | 已实现 |
| Bloom Contribution（Bloom 贡献） | 控制某个效果向 HDR 高亮额外贡献多少能量，从而影响它产生光晕的程度。 | Beam Shader 在进入全局 Bloom 提取前独立增加光束 HDR 增益，不改变求解方向。 | 已实现 |
| IOR Override（折射率覆盖） | 临时用一个统一 IOR 替代资产材质中的 IOR，常用于调试或交互演示。 | Prism-4 把 Preset/Inspector 的 Central IOR 同时用于 CPU 光路和玻璃 Shader，防止两边参数不一致。 | 已实现 |
| Optical Path Debug View（光路调试视图） | 把不可见的数学射线、交点、法线和能量画到场景上，帮助判断折射方向为何变化。 | 独立 HDR Overlay Pass 绘制逐波长路径；表格显示 Entry/Exit/Total Transmittance，TIR 使用橙色。 | 已实现 |
| GPU Debug Group | 给一组 GPU 命令加可读名称，便于在 RenderDoc、Nsight 或驱动回调中定位。 | Beam Pass 将 Incident、Internal、Exit 三批绘制分别标记。 | 已实现 |

## 10. 调试、性能与测试

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| KHR_debug | OpenGL 驱动提供的调试消息与对象标记扩展。 | Debug 构建可接收 API 错误和性能提示。 | 已实现 |
| GPU Timer Query | 让 GPU 测量一段命令实际执行时间，避免只看 CPU 提交时间。 | 整帧使用四槽 `GL_TIME_ELAPSED` Query 环；Prism-5 用成对 Timestamp Query 单独测 Beam Pass。 | 已实现 |
| GPU Timestamp Query（GPU 时间戳查询） | 让 GPU 在命令流某一点写下自己的时钟值；两个时间戳相减可测量中间一小段 Pass。 | Beam Pass 的开始/结束各写一个时间戳，不与整帧计时互相嵌套。 | 已实现 |
| CPU-bound | 帧率主要受 CPU、驱动提交或逻辑限制，GPU 还有空闲。 | Benchmark 与 Nsight 分析时需要先判断。 | 概念 |
| GPU-bound | 帧率主要受 Shader、像素、带宽或 GPU 工作量限制。 | 高分辨率、玻璃色散和焦散可能增加 GPU 压力。 | 概念 |
| GPU Capture | 把一帧的 Draw Call、资源和管线状态保存下来逐步检查。 | 计划使用 RenderDoc/Nsight 留下可复现证据。 | 计划 |
| RenderDoc | 跨 API 帧调试工具，可查看事件、纹理、Buffer、Shader 输入输出。 | 作品集性能/正确性报告计划使用。 | 计划 |
| Nsight Graphics | NVIDIA 的 GPU 调试、Trace 和 Shader Profiling 工具。 | 计划用于定位 GPU 瓶颈并记录优化前后数据。 | 计划 |
| Benchmark（基准测试） | 在固定场景、参数和硬件下重复测量，便于比较改动。 | Prism-5 固定 1080p/4x MSAA，关闭 VSync，预热 60 帧并采样 180 帧。 | 已实现 |
| Warm-up（预热） | 正式计时前先运行若干帧，让 Shader 变体、缓存、资源上传和驱动状态稳定。 | Prism-5 丢弃前 60 帧，避免首次运行成本污染 P50/P95。 | 已实现 |
| P50 / P95 | 50%/95% 样本不超过的耗时；P50 表示典型水平，P95 更能反映较慢尾部。 | Benchmark JSON 同时保存 CPU Optics、CPU Frame、GPU Frame 和 GPU Beam 的两个分位数。 | 已实现 |
| Smoke Test（冒烟测试） | 用最短流程确认程序能启动、加载和渲染，不保证所有细节正确。 | `gpu-smoke` 使用真实隐藏 OpenGL 窗口渲染 5 帧。 | 已实现 |
| Regression Test（回归测试） | 验证新改动没有破坏此前正常功能。 | CTest 覆盖资产、排序和光学；Prism-5 另有真实 GPU 视觉矩阵。 | 已实现 |
| Visual Regression（视觉回归） | 固定场景输出图片并与基准图比较，发现渲染结果变化。 | Prism-5 自动重拍 10 个 1080p 场景并与版本化基线比较。 | 已实现 |
| Baseline Image（基准图） | 在固定场景、镜头和参数下保存的参考画面，后续改动都与它比较。 | `docs/images/prism0_baseline.png` 记录色散光路接入前的 Prism-0 构图。 | 已建立 |
| MAE（平均绝对误差） | 把对应像素通道的绝对差取平均；0 表示两张图完全相同，越大表示整体偏差越明显。 | PNG 比较器使用归一化 RGBA MAE，并结合 Changed-pixel Fraction 避免局部大变化被平均掩盖。 | 已实现 |
| Changed-pixel Fraction（变化像素比例） | 统计有明显通道变化的像素占整张图多少。 | 任一通道差超过 8/255 就算变化像素；跨驱动默认阈值为 8%。 | 已实现 |
| Deterministic Capture（确定性录制） | 固定镜头、参数时间线、帧数和输出分辨率，使每次录制可重复。 | Prism-5 输出 360 帧固定时间线，再编码为 15 秒 Demo Reel。 | 已实现 |
| Demo Reel | 用短视频集中展示作品中最有代表性的视觉效果与技术变化。 | `prism5_demo_reel.mp4` 依次展示零色散、色散渐变、角度扫描和七色 Hero Shot。 | 已实现 |
| SSIM | 比逐像素相等更关注结构相似度的图像比较指标。 | 计划用于容忍不同 GPU 的微小浮点差异。 | 计划 |
| Debug View（调试视图） | 把法线、深度、粗糙度等中间数据直接显示出来。 | Glass 支持折射/厚度/色散等视图；GP-P1A 可直接显示 G-Buffer 的 Albedo、Encoded Normal、Metallic/Roughness 与 Depth，并绕过后处理。 | 已实现 |
| Overlay Pass（叠加 Pass） | 在主体画面之后再绘制一层辅助内容，通常用于轮廓、Gizmo、调试线或 HUD。 | Optical Path Debug 在 Glass 之后、Tone Mapping 之前写入同一个 HDR Scene。 | 已实现 |
| Camera Lock（镜头锁定） | 暂时禁止鼠标改变相机，避免录屏或回归截图因误操作改变机位。 | Prism-4 默认锁定 Hero Camera，取消勾选后恢复 Orbit 操作。 | 已实现 |
| State Snapshot / Restore（状态快照 / 恢复） | 进入临时模式前保存一份设置，退出时原样还原，避免该模式的开关和参数污染后续场景。 | Prism Preset 保存通用 Renderer/Scene 状态；成功加载其他模型时恢复，并自动移除光束与光路 Overlay。 | 已实现 |
| Hero Shot（主视觉镜头） | 专门为展示效果设计、参数固定且可重复恢复的代表性构图。 | Prism Demo 可一键恢复固定目标、方位、距离与 FOV。 | 已实现 |
| Overdraw | 同一像素在一帧内被重复绘制多次，透明与粒子常导致高 Overdraw。 | TA/性能调试视图计划覆盖。 | 计划 |
| GPU Memory / VRAM | GPU 用于纹理、Buffer 和 RenderTarget 的显存。 | Prism-5 统计 RenderTarget、Bloom、MSAA、Shadow、Cubemap、Beam、几何与纹理的可解释估算；不含驱动内部开销。 | 已实现（估算） |

## 11. 路线图中的现代渲染术语

| 术语 | 通俗解释 | 在 Iris 中的作用 | 状态 |
| --- | --- | --- | --- |
| G-Buffer | Deferred Rendering 用的一组纹理，先记录“这个像素是什么材质、朝哪里、离相机多远”，再统一算光照。 | GP-P1A 使用 Albedo、Encoded Normal、Metallic/Roughness、Depth/Stencil 四个 Attachment，并支持 1×/4× MSAA Resolve。 | 已实现 |
| Deferred Shading（延迟着色） | 几何阶段先写 G-Buffer，把光照推迟到全屏 Lighting Pass；大量灯光时避免为每个物体重复跑完整材质光照。 | Renderer 可与原 Forward 路径实时切换，并复用 PBR、IBL、Shadow、Caustics 与最多 64 个局部灯。 | 已实现 |
| MRT（Multiple Render Targets） | 一次片元着色同时写多张 RenderTarget，像一次填写多列表格。 | G-buffer Fragment Shader 一次写 Albedo、Normal 和 Metallic/Roughness，Depth 由固定管线写入。 | 已实现 |
| Lighting Pass（光照阶段） | 读取屏幕上的材质和深度数据，对每个可见像素统一计算光照。 | 全屏三角形从 Depth 重建世界坐标并计算 Cook-Torrance GGX、Split-Sum IBL 与阴影。 | 已实现 |
| Hybrid Deferred（混合延迟渲染） | 不透明物使用 Deferred，必须排序或读取背景的透明物仍走 Forward。 | 保留 Glass-4 的 Forward Transparent/Refractive Pass，避免把有序透明错误塞进普通 G-Buffer。 | 已实现 |
| Render Path（渲染路径） | 从场景数据到最终像素所选择的一套 Pass 流程。 | GUI 的 Opaque render path 可在 Forward 与 Deferred (hybrid) 间即时切换。 | 已实现 |
| Point Light（点光源） | 从一个位置向四周发光，亮度随距离衰减，类似未考虑灯罩的裸灯泡。 | GP-P1B 使用有限半径平滑截止的逆平方衰减；8/32/64 档中各有一半点光。 | 已实现 |
| Spot Light（聚光灯） | 只在一个锥形方向内发光，并在内外锥之间平滑变暗。 | 与 Point Light 共用距离衰减，另以 Smoothstep Cone Attenuation 控制边缘。 | 已实现 |
| Light Stress Test（光源压力测试） | 固定物体、镜头和材质，只增加灯光数量，观察渲染成本如何增长。 | 100 个独立物体配 8/32/64 局部灯，对比 Forward/Deferred GPU P50/P95、Draw Call、显存与流量。 | 已实现 |
| Light Volume（光体积） | 只在点光/聚光真正能影响的空间范围绘制光照，避免全屏像素遍历无关灯光。 | 当前 Deferred 仍对每个几何像素遍历全部局部灯；Light Volume/Tiled/Clustered 是后续优化。 | 计划 |
| Attachment Traffic（附件流量） | RenderTarget 每帧因写入、读取和 MSAA Resolve 产生的数据量。 | Benchmark 根据格式和采样数输出下限估算；明确不是驱动硬件带宽 Counter。 | 已实现（估算） |
| SSAO | 根据屏幕深度和法线近似物体缝隙中的环境遮蔽。 | GP-P1D 使用 16 样本观察空间半球与 5×5 深度感知滤波，只调制 Ambient/IBL。 | 已实现 |
| SSR | 在屏幕深度/颜色中追踪反射，只能反射屏幕已有信息。 | GP-P1 候选效果。 | 计划 |
| TAA | 融合当前帧与历史帧降低锯齿和闪烁，需要运动信息与稳定策略。 | GP-P1D 在 HDR 后处理前执行，使用深度拒绝与 3×3 Neighborhood Clamp 控制拖影。 | 已实现 |
| Motion Vector（运动向量） | 记录像素从上一帧到当前帧移动了多少。 | G-Buffer RGBA16F Attachment 同时覆盖相机、刚体 Model 与骨骼 Joint Palette 的上一帧位置。 | 已实现（相机/对象/骨骼） |
| Scene Graph（场景图） | 用父子层级组织场景物体；父对象移动时子对象跟随。 | 轻量 Scene/Entity/Transform 支持层级、显隐、选择、复制和共享 Mesh 实例。 | 已实现 |
| Shader Hot Reload（着色器热重载） | 程序运行时重新编译改过的 Shader，不必重启应用。 | 候选 Program 成功后才替换；失败保留旧 Program 并显示日志。 | 已实现 |
| Render Pass Context | 每个渲染阶段对输入、输出、Viewport、清理和图形状态的显式声明。 | RenderPassSequence 在进入 Pass 前应用 Context 与 State Cache，并输出 Debug Label。 | 已实现（轻量） |
| Scene Snapshot（场景快照） | 把某一时刻的场景、相机、灯光和资产引用冻结成只读数据，后台渲染期间不受编辑操作影响。 | SR-P1A 从实时 Scene 导出共享 `ModelData` 与 Mesh Instance，供 CPU Reference Path Tracer 使用。 | 已实现 |
| Ray Intersection（射线求交） | 判断一条带方向和有效距离的射线最先撞到哪个几何表面。 | SR-P1A 支持 Ray/AABB 和双面 Ray/Triangle，并输出重心坐标、UV 与法线。 | 已实现（CPU） |
| Surface Interaction（表面交互） | 汇总一次命中的位置、距离、几何/着色法线、UV、朝向及材质身份，是后续 BSDF 计算的输入。 | 几何求交会保留 Asset/Instance/Mesh/Material/Primitive ID。 | 已实现（CPU） |
| BVH（Bounding Volume Hierarchy） | 用层级包围盒排除大量不可能命中的三角形，减少逐三角形测试。 | SR-P1A 使用确定性的最大质心轴 Median Split，并优先遍历近节点。 | 已实现（CPU Median Split） |
| Next Event Estimation（NEE） | 在表面主动选择一个光源并发射阴影射线，而不是只等待随机反弹碰巧撞到光源。 | SR-P1D/F 统一采样发光三角形、显式灯光与 HDR Environment，显著降低小亮区噪声。 | 已实现 |
| Multiple Importance Sampling（MIS） | 将不同采样策略按各自 PDF 加权组合，减少某一种策略在不擅长区域产生的高方差。 | Power Heuristic 同时配对 BSDF 与面光、HDR Environment 的采样/命中贡献。 | 已实现 |
| Delta Light（Delta 光源） | 只从唯一方向或位置贡献能量、在立体角上没有有限面积的理想光源。 | CPU 参考路径把方向光、点光和聚光灯作为 Delta Light，只用 NEE 估计，不与 BSDF 方向 PDF 混合。 | 已实现 |
| Dielectric Transmission（绝缘体透射） | 光在非金属透明材质的界面按反射或折射继续传播。 | SR-P1G 按 `Transmission × (1-Metallic)` 选择光滑 Delta 介质分支，保留剩余不透明 PBR 分支。 | 已实现（CPU 光滑介质） |
| Exact Dielectric Fresnel | 根据入射角与界面两侧 IOR 精确计算非偏振反射率；无法折射时结果为 1。 | CPU 参考路径用它选择镜面反射、Snell 折射或全反射，并对 Delta 路径跳过连续 PDF 的 MIS。 | 已实现 |
| Beer-Lambert Volume（比尔-朗伯体积吸收） | 光在吸收介质中传播越远，各颜色通道按指数规律衰减越多。 | SR-P1G 使用 `attenuationColor^(distance/attenuationDistance)`，在闭合材质的进入与退出界面之间累计真实世界距离。 | 已实现（单层介质） |
| AOV（Arbitrary Output Variable） | 把 Beauty 之外的材质、几何或光照中间量保存为独立图像，便于定位误差和后期合成。 | SR-P1H 同步输出 Albedo、Normal、Depth、Direct、Indirect、Sample Count 与 Variance 的 HDR/PNG。 | 已实现（CPU） |
| Direct / Indirect AOV | 把相机到第一表面的直接贡献与经过一次以上散射的间接贡献分开。 | 两者逐样本相加可重建 Beauty；第一表面 NEE 和直接可见光源属于 Direct。 | 已实现（CPU） |
| Sample Variance（样本方差） | 衡量同一像素不同 Monte Carlo 样本波动大小；高值通常意味着噪声仍明显。 | 对线性 Beauty 的 Rec.709 亮度保存一/二阶矩，并以 `N-1` 输出无偏估计，供后续 Adaptive Sampling 使用。 | 已实现（CPU） |
| Tile Scheduling（分块调度） | 把图像切成小矩形任务，让多个线程动态领取，减少快慢像素造成的线程空闲。 | SR-P1I 默认按 16×16 row-major Tile 建表，Worker 用原子索引领取，完成后仍按像素顺序提交。 | 已实现（CPU） |
| Persistent Thread Pool（持久线程池） | 创建一次工作线程并重复使用，避免每轮渲染反复创建和销毁线程。 | `TileThreadPool` 跨全部 SPP Pass 保留线程，支持自动/指定 Worker 和协作取消。 | 已实现（CPU） |
| Progress Publication Throttling（进度发布节流） | 限制大型中间结果复制给 UI/调用方的频率，同时不降低内部渲染速度。 | `RenderTask` 默认每 100 ms 发布一次完整 Beauty/AOV，结束状态强制发布。 | 已实现（CPU） |
| BVH Traversal Profile | 统计射线遍历时测试过多少包围盒和三角形，用数据判断加速结构是否有效。 | SR-P1I 分别记录 Path/Shadow Ray、Bounds/Triangle Test，并与 BVH 构建时间、节点和深度一同输出。 | 已实现（CPU） |
| Surface Area Heuristic（SAH） | 用候选子节点的表面积和 Primitive 数估计未来遍历成本，比单纯从中间切开更贴合空间分布。 | SR-P1J 使用 16-bin SAH，将固定场景 Triangle Test 降低约 62.5%，并保留 Median 对照。 | 已实现（CPU） |
| Conservative Bounds（保守包围盒） | 包围盒需略大于真实几何，避免浮点舍入让本应命中的射线在 BVH 上层被错误剔除。 | Triangle Bounds 按坐标尺度增加极小 Padding，保证 Median/SAH 对掠射光线结果一致。 | 已实现（CPU） |
| Stable Hit Tie-break（稳定命中裁决） | 多个表面距离几乎相同时使用固定身份决定结果，而不是依赖遍历先后。 | SR-P1J 在尺度相关近等距窗口内选择较小 Primitive ID，使不同 BVH 树形输出一致。 | 已实现（CPU） |
| BLAS（Bottom-Level Acceleration Structure） | 为一份局部空间 Mesh Geometry 建一次加速结构，供多个刚体实例共享。 | SR-P1K 按 Asset/Mesh 去重；400 个立方体只保存一份 12 Triangle BLAS。 | 已实现（CPU） |
| TLAS（Top-Level Acceleration Structure） | 在场景层组织带 Transform 的实例包围盒，命中后把 Ray 变换到对应 BLAS 的局部空间。 | SR-P1K 使用确定性单实例叶 TLAS，并把命中法线、切线、材质和稳定 Primitive ID 还原到世界空间。 | 已实现（CPU 静态快照） |
| Acceleration Auto Selection（加速结构自动选择） | 根据场景是否真正复用几何选择单层世界 BVH 或 BLAS/TLAS，避免一种结构在所有场景中硬套。 | 展开 Triangle 数至少为唯一 Triangle 数两倍时选择 BLAS/TLAS，否则保留 World SAH BVH。 | 已实现（CPU） |
| Adaptive Sampling（自适应采样） | 低噪声像素提前停止，把路径预算留给玻璃、间接光等仍未收敛区域。 | SR-P1L 每隔固定 SPP Batch 用亮度样本均值/方差估计 95% 置信区间，并为每像素保存独立 Sample Count。 | 已实现（CPU） |
| Confidence Interval（置信区间） | 用样本方差与数量估计当前像素均值的不确定范围，而不是直接把低亮度误判为低噪声。 | 停止条件为 `1.96 × sqrt(variance / N) <= max(relative × abs(mean), absolute)`。 | 已实现（CPU 亮度） |
| Cross-renderer Comparison（跨渲染器对照） | 用同一场景、相机和显示变换比较实时 Raster 与离线 Path Tracer，定位算法近似差异。 | SR-P1M 输出 Raster、ACES/sRGB Path Traced、原始 4× Difference、5×5 Median 展示 Difference、Triptych、未滤波 JSON 指标和 AOV。 | 已实现 |
| PSNR（Peak Signal-to-Noise Ratio） | 将图像 RMSE 转为分贝；数值越高表示显示空间误差越小，零误差时为无穷大。 | SR-P1M 与 MAE、RMSE、Changed Fraction 一起报告，但不把不同渲染算法设成像素等价门槛。 | 已实现（诊断） |
| Temporal Reprojection（时序重投影） | 根据运动向量把上一帧结果映射到当前帧。 | TAA 使用上一帧 Color/Depth ping-pong，越界或深度不兼容时拒绝历史。 | 已实现 |
| Jitter | 每帧轻微移动投影采样位置，用多帧积累获得更密集采样。 | TAA 使用 8 样本 Halton(2,3) 序列偏移投影矩阵。 | 已实现 |
| Frustum Culling（视锥剔除） | CPU/GPU 不提交相机视野外的物体。 | GP-P1C 从 View-Projection 提取六个平面，以世界空间包围球保守判断 2,500 个实例的可见性。 | 已实现（CPU） |
| LOD（Level of Detail） | 根据距离或屏幕尺寸选择不同精度模型。 | GP-P1C 按投影像素半径选择三档顶点聚类索引；共享 Vertex Buffer，只切换 Element Buffer。 | 已实现（三档） |
| Instancing（实例化） | 一次 Draw Call 绘制同一 Mesh 的多个不同 Transform 实例。 | GP-P1C 以 `glDrawElementsInstanced` 和 Mat4 Instance Buffer 合并同模型、Tint、LOD 的对象。 | 已实现 |
| Skinning（骨骼蒙皮） | 用多个关节变换按顶点权重混合，让网格随骨架变形。 | GP-P1E 每顶点最多四权重，由 Vertex Shader 执行 Linear Blend Skinning。 | 已实现（GPU） |
| Bind Pose（绑定姿势） | 网格绑定到骨架时的基准姿势，是动画与逆绑定矩阵计算的起点。 | 关闭动画后仍应用 Bind Joint Palette；固定回归确认模型不跳变。 | 已实现 |
| Inverse Bind Matrix（逆绑定矩阵） | 把绑定姿势顶点从 Mesh 空间转换到某个关节的局部绑定空间。 | 每个 Mesh 独立保存并按其烘焙节点变换校正，再与动画关节 Global Matrix 相乘。 | 已实现 |
| Animation Sampling（动画采样） | 在当前时间从相邻关键帧计算节点的位移、旋转和缩放。 | Translation/Scale 线性插值，Rotation 使用 Quaternion Slerp，Clip 到末尾后循环。 | 已实现（单 Clip） |
| Skin Palette（蒙皮矩阵调色板） | 一组供 Vertex Shader 按 Joint Index 查找的关节矩阵。 | OpenGL 3.3 Uniform Array 每 Mesh 最多上传 64 个 `mat4`。 | 已实现 |
| GPU-driven Rendering | 由 GPU 完成可见性、批次和间接绘制命令生成，减少 CPU 提交。 | GP-P2 备选旗舰。 | 计划 |
| Visual Regression（视觉回归） | 用固定场景、固定机位和容差比较重拍图片，确认代码修改没有破坏既有画面。 | Glass-4 已用 14 张 1080p 基线覆盖玻璃、色散、焦散和 MSAA；属于[回归测试](https://vibe-hub.org/regression-test)的一种。 | 已完成 |
| Frame Capture（帧捕获） | 保存一段 CPU/GPU 图形调用、时间线和调试标记，供 RenderDoc、Nsight 等工具离线检查。 | Glass-4 保存带 `KHR_debug` Pass 范围的 Nsight Systems 报告。 | 已完成 |
| GPU Timestamp Query | 在 GPU 命令流中记录开始/结束时间戳，不阻塞 CPU 地统计某个 Pass 的耗时。 | Renderer 为所有顶层 Pass 使用 4 槽查询环，并在 GUI/Benchmark 输出结果。 | 已完成 |
| Compute Shader | 不直接负责画三角形、用于通用 GPU 并行计算的 Shader。 | OpenGL 3.3 不支持；GPU 粒子/Driven 路线需升级 API。 | 计划 |
| SSBO | Shader 可读写的大容量结构化 Buffer。 | OpenGL 4.3+ GPU-driven/Compute 路线需要。 | 计划 |
| Indirect Draw | Draw Call 的参数存放在 GPU Buffer 中，可由 GPU 生成。 | GPU-driven 路线需要 Multi-Draw Indirect。 | 计划 |
| Vulkan | 显式管理资源、同步和命令提交的现代低层图形 API。 | GP-P2 推荐旗舰后端。 | 计划 |
| Dynamic Rendering | Vulkan 中不预先创建固定 RenderPass/Framebuffer 组合，运行时声明附件。 | 计划作为 Vulkan 后端基线。 | 计划 |
| Timeline Semaphore | 用递增数值表达 Vulkan GPU 工作依赖的同步对象。 | 计划用于 Vulkan Frame-in-flight 与资源上传同步。 | 计划 |

## 12. 容易混淆的概念

### Alpha Blending 与 Transmission

- Alpha Blending：把已经算好的前景颜色与背景颜色按比例混合，像把两张图片叠在一起。
- Transmission：光真正穿过材质，可能发生折射、吸收和内部反射。
- 结论：实现 glTF `alphaMode=BLEND` 并不等于实现玻璃。

### Chromatic Aberration 与 Spectral Dispersion

- Chromatic Aberration：镜头或全屏后处理让 RGB 通道在画面边缘错位。
- Spectral Dispersion：水晶内部不同波长按照不同 IOR 折射。
- 结论：参考图中的水晶彩边应由材质色散产生，而不是只做全屏 RGB 偏移。

### Bloom 与 Caustics

- Bloom：把已经存在的高亮区域扩散成光晕。
- Caustics：反射/折射光线聚集后，真的在地面形成高能量图案。
- 结论：Bloom 可以让焦散更耀眼，但不能凭空生成焦散形状。

### MSAA 与 TAA

- MSAA：同一帧对几何边缘做多重覆盖采样。
- TAA：融合多帧信息，能处理更多 Shader 锯齿和闪烁，但会面临拖影。
- 结论：当前 4x MSAA 能改善边缘，未来移动焦散仍可能需要 TAA 或专用时序稳定。

### HDR Texture 与 HDR Display

- HDR Texture：渲染过程中使用浮点纹理保存高亮能量。
- HDR Display：显示器和操作系统以 HDR 标准真正输出更大亮度范围。
- 结论：本项目目前实现的是 HDR 渲染中间结果，最终仍 Tone Map 到普通 LDR/sRGB 输出。

### Material 与 Shader

- Shader：定义“怎样计算”的 GPU 程序。
- Material：提供“用哪些参数和纹理计算”的数据实例。
- 结论：多个玻璃 Preset 可以共享一个 Glass Shader，但使用不同 IOR、Thickness、Dispersion 等材质参数。

## 13. 主要代码位置速查

| 想查什么 | 主要文件 |
| --- | --- |
| 帧流程与 Pass 顺序 | `src/render/Renderer.cpp`、`src/render/RenderPassSequence.h` |
| HDR/MSAA/Depth/Framebuffer | `src/render/RenderTarget.*` |
| Bloom、Tone Mapping、最终输出 | `src/render/PostProcessor.*`、`shaders/postprocess.frag` |
| PBR、IBL、法线贴图、阴影采样 | `shaders/basic.frag` |
| 顶点变换与切线空间输入 | `shaders/basic.vert` |
| 环境 Cubemap 与天空盒 | `src/render/EnvironmentMap.*`、`shaders/skybox.frag` |
| Shadow Map | `src/render/ShadowMap.*`、`shaders/shadow_depth.*` |
| 彩色透射阴影与焦散 | `src/render/ShadowMap.*`、`src/render/CausticsMap.*`、`shaders/caustics_*`、`shaders/transmission_shadow.*` |
| GPU Mesh 与材质绑定 | `src/render/Mesh.*`、`src/render/GpuModel.*` |
| 实例化、视锥剔除与 LOD | `src/render/SceneDrawList.*`、`src/render/Mesh.*`、`docs/instance-culling-lod.md` |
| 纹理解码、缓存和回退 | `src/render/Texture2D.*` |
| 格式无关资产结构 | `src/asset/ModelData.h` |
| OBJ 导入 | `src/io/ObjLoader.*` |
| DAE/glTF/GLB 导入 | `src/io/AssimpImporter.*` |
| glTF/GLB 色散扩展适配 | `src/io/GltfMaterialExtensions.*` |
| 棱镜求交与连续光谱 | `src/optics/PrismOptics.*` |
| Prism 参数、Preset 与 White Point | `src/optics/PrismDemo.*` |
| 光束 Ribbon 网格与 HDR 绘制 | `src/optics/SpectralBeamMesh.*`、`src/render/SpectralBeamRenderer.*`、`shaders/spectral_beam.*` |
| 光路、交点、法线与能量调试层 | `src/render/OpticalPathDebugRenderer.*` |
| UI、加载与主循环 | `src/app/Application.*` |
| CPU 资产回归测试 | `tests/AssetImportTests.cpp` |
| Prism 公式、数据流与近似边界 | `docs/prism-spectrum.md` |
| Prism 视觉回归、性能与作品集证据 | `docs/prism5-validation.md`、`tests/ImageComparison.cpp`、`tools/Prism5*.cmake` |
| Glass-3 焦散算法、边界与性能 | `docs/glass3-caustics.md`、`tools/Glass3*.cmake` |
| 后续阶段与验收标准 | `todolist.md` |
