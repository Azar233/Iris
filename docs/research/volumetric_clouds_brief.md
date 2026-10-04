# 实时体积云渲染研究简报（Iris / OpenGL 3.3 Core）

> 文档性质：调研输入（research brief），**不是**阶段实施记录，因此不含截图与实测数字；其中的成本、参数与收益均为公开资料的量级估计，落地时必须在本机重新标定。基准证据请见各阶段文档与 `docs/reference-path-tracer.md`。

范围：为 C++/OpenGL 3.3 + 未来 Vulkan 后端的渲染实验室提供可直接转成实施计划的事实性依据。
约束前提：无 compute shader（4.3）、无 image load/store（4.2）、无 texture view（4.2）、无 `glDispatchCompute`/`glCopyImageSubData`（4.3）；现有 Forward/Deferred PBR、TAA（motion vector + history reprojection）、SSAO、height fog、bloom、ACES、解析 Rayleigh/Mie 天空（`sunDirection`/`skyRadiance`/`sunTransmittance` 与 CPU path tracer 共享）、单张正交 shadow map、CPU 参考 path tracer。
标注含义：**C/B** = cost/benefit，★越多越划算。

---

## 1. 基础文献（标题 / 年份 / 作者 / 出处）

**行业主线（已用官方课程页与作者主页核对）**

- Andrew Schneider, *The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn*, SIGGRAPH 2015, Advances in Real-Time Rendering in Games 课程（Guerrilla Games）。PDF/PPT 见课程页；扩展章节为 *Real-Time Volumetric Cloudscapes*, GPU Pro 7（2016）。
  <https://advances.realtimerendering.com/s2015/index.html>
- Sébastien Hillaire, *Physically-based and Unified Volumetric Rendering in Frostbite*, SIGGRAPH 2015, Advances in Real-Time Rendering（课程页同一场次标题写作 "Towards Unified and Physically-Based Volumetric Lighting in Frostbite"，两种写法都指这次报告）。
- Sébastien Hillaire, *Physically Based Sky, Atmosphere & Cloud Rendering in Frostbite*, SIGGRAPH 2016, Physically Based Shading in Theory and Practice 课程（给出 multiple-scattering octave 近似与能量守恒形式）。
  <https://sebh.github.io/publications/index.html>
- Magnus Wrenninge, *Art-Directable Multiple Volumetric Scattering*, SIGGRAPH 2015 Talks（Nubis/Decima 的多重散射艺术可控性）。
  <https://history.siggraph.org/wp-content/uploads/2022/10/2015-Talks-Wrenninge_Art-Directable-Multiple-Volumetric-Scattering.pdf>
- Magnus Wrenninge, *Nubis: Authoring Real-time Volumetric Cloudscapes with the Decima Engine*, SIGGRAPH 2017, Advances in Real-Time Rendering（Kojima Productions / Decima）。slides：`realtimerendering.com/advances/s2017/`（页面本身对抓取返回 403，我未取到正文，仅确认该 URL 与文件名存在）。
- Red Dead Redemption 2 的云：**不确定**。常见引用为 "Volumetric Cloud Rendering: An In-Game Case Study"（Rockstar，作者与会议归属我无法确证；网上流传的 slides 链接可靠性未验证）。建议引用时明确写成"社区流传资料，未核实"，不要当作正式论文。

**建模与散射理论基础**

- David S. Ebert et al., *Texturing & Modeling: A Procedural Approach*（第 3 版, 2003）——体积云密度场的经典程序化建模来源。
- Steven Worley, *A Cellular Texture Basis Function*, SIGGRAPH 1996——Worley/cellular 噪声，云形状的基础。
- Ken Perlin, *Improving Noise*, SIGGRAPH 2002——Perlin 噪声与 tileable 变体。
- *Production Volume Rendering: Design and Implementation*（Wrenninge, 2012）——物理化体积散射、多重散射的生产实践参考。
- *Physically Based Rendering: From Theory to Implementation*（PBRT，第 3/4 版）——Beer–Lambert、Henyey–Greenstein、single/multiple scattering 的权威推导；建议作为 CPU path tracer 与 GPU 实现共同的一致性标准。
- *Real-Time Rendering, 4th ed.*（2018）中参与介质章节——工程折中的汇总。

**实践中的光传输模型**：Beer–Lambert 透射率 `T = exp(-∫σ_t ds)` + 单次散射积分 `L = ∫ T(s)·σ_s·p(θ)·L_sun·T_sun(s) ds`；相位函数用**双叶 Henyey–Greenstein**（前向 `g≈0.8`，后向 `g≈-0.3` 混合，混合权重常见 `0.5`）以同时得到银边（silver lining）与背向散射。HZD 与 Frostbite 都采用这一组合；powder 效应（`1-exp(-2σ d)` 形式）在薄云边缘提亮，属于可选修饰项。注意 HG 是**近似**，不是严格 Mie 解；与解析 Mie 天空的相位函数不完全一致，这正是视觉不一致的常见来源。

---

## 2. 核心算法流水线（实现顺序）

| # | 阶段 | 解决的问题 | 标准做法 | 成本 / 收益 |
|---|---|---|---|---|
| 1 | 离线生成 tileable 3D 噪声 | 程序化噪声在 shader 里逐帧算太贵（Worley 需 27 邻域） | CPU 生成 32³–64³ Worley + Perlin FBM，`glTexImage3D`/`glTexSubImage3D` 上传 | 一次性；**C/B ★★★**（且对确定性最友好） |
| 2 | Weather map（2D coverage/type） | 让美术用一张图控制云量、云型、高度 | 2D 纹理 R=coverage，G=cloud type，B=height/wetness（HZD 方案） | 极低；**★★★**。是"能调"与"不能调"的分界 |
| 3 | 密度重映射 + 侵蚀 | 把噪声变成有轮廓的云，而非均匀雾 | `d = saturate(remap(baseFBM, coverage))` 再用 detail noise 做边缘侵蚀（erosion），按高度做梯度衰减 | 纹理采样 + 若干 ALU；**★★★** |
| 4 | 粗步进 + 抖动 + 提前退出 | 全分辨率均匀步进成本爆炸 | 24–128 步于云层"slab"（shell）内；非均匀/球壳步长；per-frame jitter；`T<0.01` 提前退出（HZD 用 cone 采样） | 主要成本项；**★★★** |
| 5 | Sun light-march（optical depth） | 需要太阳方向的累积消光，云才有明暗与银边 | 每个有效样本向太阳走 4–6 步，累加密度→透射率；可复用主 march 的采样点 | +30–60% 于主 march；**★★★**。没有它就没有云的自阴影 |
| 6 | 多重散射近似 | 单次散射让云内部发黑、缺乏环境照明 | (a) Hillaire octave 近似：多次评价 HG + 衰减 `a^n`（`a≈0.5–0.7`），能量守恒；(b) 预计算 multi-scatter LUT（2D 纹理，CPU 生成即可） | (a) 数次额外 phase 求值，便宜；(b) LUT 便宜但维度固定。**★★★ / ★★** |
| 7 | Blue-noise / 抖动偏移 | 步进条带、帘状 artifact | 用一张 blue-noise 2D 纹理与帧序号偏移采样起点；TAA 负责把噪声积掉 | 成本可忽略；**★★★**（但破坏确定性，见 §7） |
| 8 | 时间重投影与 history 拒绝 | 半分辨率 + 抖动带来的噪声需要累积 | 用风场在云层入口深度的位移构造 volume motion vector，重投影 history；深度/法线差异 + 邻域颜色钳制（clamp）拒绝无效历史 | 一次额外全屏 pass；**★★★**。是"低分辨率可用"的前提 |
| 9 | 半/四分之一分辨率 + 深度感知升采样 | 直接全分辨率不可负担 | 体积散射与 transmittance 在 1/2 或 1/4 分辨率渲染；用低分辨率 pass 的深度做 joint-bilateral / 深度引导 upsample | 省 4–16 倍像素成本；**★★★** |
| 10 | 云阴影投影 | 云要在地面和水面上投影 | 从太阳正交视图再跑一次（廉价）raymarch 得到 transmittance，写入 shadow map 或屏幕空间缓存 | 一次全屏 march；**★★** |

---

## 3. OpenGL 3.3 Core 到底能做到哪一步

**3.3 明确没有的东西**：compute shader（4.3）、`glDispatchCompute`（4.3）、image load/store（4.2）、`glBindImageTexture`（4.2）、texture view（`glTextureView`，4.2）、`glCopyImageSubData`（4.3）、shader storage buffer（4.3）、`GL_ARB_shader_image_load_store`、`GL_ARB_compute_shader`。因此任何"在 GPU 上写 3D 纹理 / 体素网格"的方案都不成立。

**可行的替代路径**

- **CPU 生成 + `glTexImage3D` / `glTexSubImage3D`（推荐）**：把 Worley/Perlin FBM、weather map 派生数据、multi-scatter LUT 全在 CPU 生成后上传。与项目"离线可复现、无网络"的价值取向完全一致，且天然确定性。代价是内存与上传带宽：对 64³ RGBA8 ≈ 1 MB，128³ ≈ 8 MB，可接受；只上传一次即可。
- **全屏 fragment pass 代替 compute**：云的每一帧计算（raymarch、时间累积、升采样）本来就是逐像素的，用 `glDrawArrays(GL_TRIANGLES, 0, 3)` + `#version 330` 完全够用——**本方案不真的需要 compute**。compute 只在"动态生成/更新 3D 体积"时才不可替代。
- **2D-array "sliced" 3D 纹理**：`GL_TEXTURE_2D_ARRAY` 在 3.0 就有。可用于降低单一 3D 纹理的内存压力或做分层 LOD，但手动三线性插值（两次 slice 之间 lerp）会换来额外采样，通常不划算。
- **渲染到 FBO 附件再采样**：`GL_RGBA16F` 颜色附件在 3.3 可渲染；`glFramebufferTextureLayer`（3.0）可对 array/cubemap 的指定层渲染——这允许"用 fragment shader 把 2D 切片写进 2D array"，但**不能写 3D 纹理层**，所以它替代不了 3D 体素更新。
- **常用扩展的可用性**：`GL_ARB_texture_storage`（4.2 核心，扩展在 3.3 上广泛可用，建议探测后再用）提供不可变纹理，利于确定性；`GL_ARB_debug_output`/`GL_KHR_debug`、timer query（`GL_ARB_timer_query`）等与云无关但利于性能取证。**不要**依赖任何 compute/atomics 相关扩展——即使某些驱动暴露它们，也会破坏"3.3 Core 基线"这一契约。
- 若未来 Vulkan 后端可用，compute 的合理用途只有：动态 3D 噪声生成、体积雾级联体素化、multi-scatter LUT 增量更新。云的 raymarch 主体留在 fragment pass 更简单。

---

## 4. 与现有引擎特性的集成风险

**TAA / reprojection ghosting（最大风险）**
云是低对比、大面积、缓慢移动的内容，恰好是 TAA ghosting 最明显的场景。做法：(a) 为云体积生成**独立的 volume motion vector**——取"云层入口深度"处的风场位移，而不是几何 motion vector；(b) history 拒绝用低分辨率 pass 自身的深度差 + 邻域 min/max clamp；(c) 相机静止时也允许累积，但相机旋转时必须按角速度限制 history 权重；(d) 云是唯一"薄"内容，可考虑云缓冲单独累积（与几何 TAA 解耦），避免污染几何的 history。注意云和几何的 TAA 混在一个 history buffer 里时，云的半分辨率噪声会渗到前景几何上。

**深度感知升采样**
现有只有 opaque depth buffer，这对云是**够用但不完美**的：云通常位于远景，不与近处几何交错；用低分辨率 pass 写出的"首个有效样本深度"（首个非零密度处）作为引导深度比用 opaque depth 更稳。风险是天空像素 depth = far，升采样会把云的边缘与天空混在一起——需要用 transmittance 的 alpha 做加权，而不是纯深度双线性。

**height fog / 空中透视**
顺序很关键。物理一致的做法：让云的 raymarch 自己沿视线累积大气消光（复用 `sunTransmittance` / `skyRadiance`），并在云之后对**几何**应用 aerial perspective；height fog 只作用于几何部分，不作用于云。低成本但不一致的替代：先用现有 height fog 合成几何，再把云以 `1-T` 为 alpha 混上去——云会被 fog 错误地"盖住"。**推荐前者**，并在文档里写明 height fog 是风格化近似、与解析天空的物理模型并不严格一致（这点项目已有类似声明，保持一致）。

**单张正交 shadow map 的一致性**
云不进入几何 shadow map，因此云与地面阴影会"不同步"。太阳方向的**解析一致性**是很容易做对的：云的 sun light-march、地面的 shadow map、`sunTransmittance` 三者必须用同一个 `sunDirection` 与其高度相关的太阳辐照度，否则黄昏时云被照亮而地面已经变暗，观感会立刻崩。云对几何的投影需要 §2 第 10 项的额外 pass；不要试图复用几何 shadow map（正交范围只覆盖相机附近，且不含云密度）。

**CPU path tracer 与 GPU 的一致性**
如果 CPU path tracer 不实现云，而 GPU 渲染里有云，固定相机 baseline 的主光方向/阴影期望值会系统性不一致。两个诚实选项：(a) CPU path tracer 里实现同一份 slab 密度函数做参考渲染（推荐的"golden reference"，成本低、可交叉验证）；(b) 明确声明云是 GPU-only 效应，参考图不含云，并在文档中记录该偏差。

---

## 5. 分阶段实施路径

**(a) 解析/2D 云层**（最便宜）
需求：一张 tileable 2D 噪声 + 在天空方向上的解析投影；无需新 pass 结构。
验证：与解析天空叠加后的固定相机 baseline；CPU 侧可解析复算同一函数。
局限：无厚度、无视差、无自阴影，抬头看云"贴在天上"。**C/B ★★★ 作为占位与回归基线极划算。**

**(b) Raymarched slab + 程序化噪声，无多重散射**
需求：`glTexImage3D` 上传的 3D 噪声、slab 边界（base/top height）、24–48 步、coverage 重映射；无时间累积（纯抖动会明显噪）。
验证：与 (a) 对比形状连续性；与 CPU 参考 raymarch（同一密度函数、同一采样方案）做数值对比。
局限：无自阴影故扁平；抖动噪声明显；开销随步数线性增长。

**(c) Light march + 多重散射近似 + 半分辨率 + 时间累积**
需求：半分辨率渲染目标 + 深度引导 upsample；volume motion vector 接入现有 TAA 管线；blue-noise 纹理。
验证：新增 Low/High 两档；用稳定相机下的多帧一致性（闪烁度量）与 baseline 对比。
局限：运动时 ghosting；多重散射近似会导致阴影侧偏亮/偏灰；时间累积在快速相机运动下退化。

**(d) Weather map 授权与云型预设**
需求：CPU 侧 weather map 生成/导入（离线资产）；coverage/type/height 三通道；Low/High 两档参数预设（cumulus / stratus / cirrus）。
验证：同一 weather map 在 Low/High 下结构一致；CPU 参考图使用同一 weather map。
局限：cirrus 这类薄高层云与 slab 模型不匹配（需要单独的高层薄层或不同的密度剖面）；"云型"参数化的视觉空间很窄，容易调到不自然。

**(e) 云阴影与 god rays**
需求：太阳正交视图的额外 raymarch pass（可 1/4 分辨率 + 时域复用）；水面/地面的云影调制；god rays 用屏幕空间径向散射（复用 shadow/transmittance 缓冲）。
验证：云影位置与太阳方向、时间的一致性；god rays 与 shadow map 的一致性检查。
局限：屏幕空间 god rays 在遮挡边缘会漏光（经典 artifact）；云影低分辨率会抖动，需要额外时域滤波。

---

## 6. 数值与参数参考（**强硬件相关，仅作起点**）

| 参数 | 常见范围 | 说明 |
|---|---|---|
| 云底/云顶高度 | 1.5–3 km / 4–8 km | 积云；HZD 类方案用两个高度参数插值 |
| σ_t（消光系数） | 薄云 ~0.01–0.1 /km 级；浓积云可到 0.1–1 /km 量级 | 不同资料差一个数量级，**以视觉标定为准**；真正有意义的是 `σ_t × 路径长度`（optical depth），HZD 场景里云的光学厚度常在 10–100 |
| density 基础值 | 0–1 归一化，乘 σ_t | 归一化后再乘系数更易调参 |
| 主 march 步数 | Low 24–48；High 64–128（半分辨率） | 与分辨率相乘才是真实成本 |
| light march 步数 | 4–6（有时按密度自适应） | 超过 8 收益很低 |
| 时间累积系数 | history 0.9–0.97 | 越高越稳，ghosting 越重 |
| 分辨率比 | 1/2 全分辨率最常见；1/4 用于远景/低配 | 1/4 + TAA 在薄云上开始糊 |
| 多重散射衰减 `a` | ~0.5–0.7（分 octave 递减） | 需要按视觉调；与单次散射的混合比例同样敏感 |
| HG `g` | 前向 0.7–0.85；后向 −0.2 ~ −0.4；混合 ~0.5 | 银边强度主要由前向 `g` 决定 |

---

## 7. 确定性与验证

**不确定性的来源（按影响排序）**
1. **时间累积**：history 权重使第 N 帧依赖前 N−1 帧，baseline 必须定义"预热帧数"或清零 history。
2. **Blue-noise / jitter 帧偏移**：若偏移来自帧序号或时间，则帧序号必须由 24 帧固定时间轴驱动（项目已具备），不能使用 wall-clock。
3. **GPU 浮点与超越函数差异**：`exp`/`pow`/噪声实现跨厂商有 ULP 级差异，在指数密集的体积积分里会被放大；驱动也不保证一致的 fast-math。
4. **纹理过滤与精度**：3D 纹理的线性过滤在不同 GPU 上插值精度不同；`GL_RGBA16F` 与 `GL_RGBA8` 混用会引入额外差异。
5. **帧内并行的非确定性**：本方案不做 GPU 原子累加，这一项影响很小（Vulkan 后端做 compute LUT 时才会出现）。

**"确定性模式"的具体设计**
- `determinism` 开关：强制 history 权重 = 0（单帧即最终图像）、抖动使用固定序列（如 Halton / 固定 blue-noise 表，不用帧序号）、禁用任何自适应步数。
- 所有 3D 噪声与 LUT 来自 CPU 生成 + 哈希校验的离线资产，二进制内容可随 baseline 一起版本化（贴合项目"离线/无网络可复现"的价值）。
- 记录并随 baseline 一起固定：`GL_RENDERER` 字符串、云参数全集、分档（Low/High）。
- headless render-job 应显式声明预热帧与是否启用时间累积，并在输出元数据里带上这两项；否则同一 commit 在 GUI 与 headless 下会产生不同 PNG。
- 诚实文档：确定性模式**不保证**跨 GPU 厂商逐像素一致；baseline 按"给定 GPU + 给定驱动"固化，或对云层使用宽容差（例如仅对非云区域做严格 SSIM，云区域用较宽松阈值）。

**可交叉验证的手段**：CPU 参考 raymarch（复用 §5(b) 的同一密度函数）能同时验证 GPU 的密度重映射、步进与解析部分；这是本项目相对其他引擎的独特优势，建议在 (b) 阶段就建立，而不是等到最后。

---

## 参考链接

- SIGGRAPH 2015 Advances in Real-Time Rendering（含 Hillaire 2015 与 Schneider HZD 报告、PDF/PPT 下载）：<https://advances.realtimerendering.com/s2015/index.html>
- Sébastien Hillaire 出版列表（2015/2016 报告官方链接）：<https://sebh.github.io/publications/index.html>
- Hillaire 2016 Frostbite slides：<https://www.ea.com/frostbite/news/physically-based-sky-atmosphere-and-cloud-rendering>
- Hillaire 2015 Frostbite slides：<https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite>
- Wrenninge, Art-Directable Multiple Volumetric Scattering（SIGGRAPH 2015 Talks PDF）：<https://history.siggraph.org/wp-content/uploads/2022/10/2015-Talks-Wrenninge_Art-Directable-Multiple-Volumetric-Scattering.pdf>
- Nubis / Decima 云系统 slides（SIGGRAPH 2017 课程，站点对自动抓取返回 403，未取到正文）：`https://www.realtimerendering.com/advances/s2017/`
- Schneider, *Real-Time Volumetric Cloudscapes*, GPU Pro 7 章节（Taylor & Francis 链接返回 403，仅确认书目条目存在）：<https://www.taylorfrancis.com/chapters/edit/10.1201/b21261-11/real-time-volumetric-cloudscapes-andrew-schneider>
- Hillaire 2020 *A Scalable and Production Ready Sky and Atmosphere Rendering Technique*（EGSR 2020，含开源 UE 实现；对 aerial perspective 与 transmittance LUT 有直接参考价值）：<https://sebh.github.io/publications/egsr2020.pdf>、<https://github.com/sebh/UnrealEngineSkyAtmosphere>

**未核实项声明**：RDR2 云渲染的正式论文/作者/会议归属未能核实；上表中所有数值范围来自公开 talks 与社区共识的量级估计，**没有**逐条回溯到可引用的原始页码，落地时必须在本机硬件上重新标定。
