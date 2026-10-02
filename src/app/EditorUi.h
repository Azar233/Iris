#pragma once
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
namespace EditorUi {
inline constexpr ImVec2 minimumDockedPanelSize{260.0f, 120.0f};
inline bool chinese = true;
inline bool chineseFontAvailable = false;
inline void setLanguage(bool value) { chinese = value; std::ofstream("MyRenderer.language") << (value ? "zh-CN" : "en"); }
inline void initialize(ImGuiIO& io) {
  std::string language; std::ifstream("MyRenderer.language") >> language; chinese = language != "en";
  for (const char* font : {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf", "C:/Windows/Fonts/simsun.ttc"}) {
    if (std::filesystem::exists(font) && io.Fonts->AddFontFromFileTTF(font, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull())) { chineseFontAvailable = true; break; }
  }
  if (!chineseFontAvailable) chinese = false;
  io.MouseDrawCursor = false;
}
inline const char* label(const char* key) {
  static const std::unordered_map<std::string, std::string> values {
    {"Hierarchy###Hierarchy", "场景层级###Hierarchy"},
    {"Scene Explorer###Hierarchy", "场景浏览器###Hierarchy"},
    {"Inspector###Inspector", "检查器###Inspector"},
    {"Viewport###Viewport", "场景视口###Viewport"},
    {"Content Browser###Assets", "资源浏览器###Assets"},
    {"Workspace###Workspace", "工作区###Workspace"},
    {"New empty scene", "新建空场景###New empty scene"},
    {"Open scene...", "打开场景…###Open scene..."},
    {"Reopen last scene", "恢复最近场景###Reopen last scene"},
    {"Save scene", "保存场景###Save scene"},
    {"Save scene as...", "场景另存为…###Save scene as..."},
    {"File", "文件###File"},
    {"View", "视图###View"},
    {"Help", "帮助###Help"},
    {"Open model...", "导入模型…###Open model..."},
    {"Open bundled model", "导入内置模型###Open bundled model"},
    {"Open bundled scene", "打开内置场景###Open bundled scene"},
    {"Reset scene to current model", "以当前模型重置场景###Reset scene to current model"},
    {"Save viewport PNG", "保存视口 PNG###Save viewport PNG"},
    {"Exit", "退出###Exit"},
    {"Reset camera", "重置相机###Reset camera"},
    {"Reset layout", "重置布局###Reset layout"},
    {"Panels", "面板###Panels"},
    {"Scene objects", "场景对象###Scene objects"},
    {"Scene statistics", "场景统计###Scene statistics"},
    {"Shadow cascades", "阴影级联数###Shadow cascades"},
    {"Cascade split blend", "级联切分混合###Cascade split blend"},
    {"Water surface", "海面###Water surface"},
    {"Enable water", "启用海面###Enable water"},
    {"Sea state", "海况###Sea state"},
    {"Water quality", "海面质量###Water quality"},
    {"Water level", "海面高度###Water level"},
    {"Water extent", "海面范围###Water extent"},
    {"Wave amplitude", "波浪振幅###Wave amplitude"},
    {"Wave speed", "波浪速度###Wave speed"},
    {"Wave steepness", "波浪陡度###Wave steepness"},
    {"Foam strength", "泡沫强度###Foam strength"},
    {"Surface roughness", "海面粗糙度###Surface roughness"},
    {"Sky reflection", "天空反射强度###Sky reflection"},
    {"Fine ripples", "细波强度###Fine ripples"},
    {"Sun glint", "太阳高光强度###Sun glint"},
    {"Deep water", "深水底色强度###Deep water"},
    {"Wind east", "风向东分量###Wind east"},
    {"Wind north", "风向北分量###Wind north"},
    {"Show cascade regions", "显示级联区域###Show cascade regions"},
    {"Duplicate selected", "复制所选###Duplicate selected"},
    {"Delete", "删除###Delete"},
    {"Parent", "父对象###Parent"},
    {"None", "无###None"},
    {"Model assets", "模型资源（单击添加至场景）###Model assets"},
    {"Open path", "导入路径###Open path"},
    {"Browse...", "浏览并导入…###Browse..."},
    {"Load entered path", "添加到场景###Load entered path"},
    {"Status", "状态###Status"},
    {"Object", "对象###Object"},
    {"Renderer", "渲染设置###Renderer"},
    {"Transform", "变换###Transform"},
    {"Position", "位置###Position"},
    {"Rotation", "旋转###Rotation"},
    {"Scale", "缩放###Scale"},
    {"Entity tint", "对象颜色###Entity tint"},
    {"Reset transform", "重置变换###Reset transform"},
    {"Auto rotate", "自动旋转###Auto rotate"},
    {"Stage", "舞台###Stage"},
    {"Ground receiver", "地面接收器###Ground receiver"},
    {"Ground color", "地面颜色###Ground color"},
    {"Ground offset", "地面高度###Ground offset"},
    {"Comparison object", "对比对象###Comparison object"},
    {"Material", "材质###Material"},
    {"Lighting", "光照###Lighting"},
    {"Visibility", "可见性###Visibility"},
    {"Casts shadow", "投射阴影###Casts shadow"},
    {"Object actions", "对象操作###Object actions"},
    {"Base color tint", "基础颜色###Base color tint"},
    {"Asset statistics", "主模型资源统计###Asset statistics"},
    {"Shader development", "着色器开发###Shader development"},
    {"GPU skinning & animation", "GPU 蒙皮与动画###GPU skinning & animation"},
    {"PBR & environment", "PBR 与环境###PBR & environment"},
    {"Post processing", "后期处理###Post processing"},
    {"Rasterization", "光栅化###Rasterization"},
    {"Directional light", "平行光###Directional light"},
    {"Camera", "相机###Camera"},
    {"Runtime", "运行信息###Runtime"},
    {"Lighting & environment", "光照与环境###Lighting & environment"},
    {"Render diagnostics", "渲染诊断###Render diagnostics"},
    {"Frame", "聚焦###Frame"},
    {"Save PNG", "保存 PNG###Save PNG"},
    {"Grid", "网格###Grid"},
    {"Ground", "地面###Ground"},
    {"Axes", "坐标轴###Axes"},
    {"Frame model", "聚焦主模型###Frame model"},
    {"Wireframe", "线框###Wireframe"},
    {"Back-face culling", "背面剔除###Back-face culling"},
    {"Ground grid", "地面网格###Ground grid"},
    {"Ground plane", "地面###Ground plane"},
    {"XYZ axes", "XYZ 坐标轴###XYZ axes"},
    {"About MyRenderer", "关于 MyRenderer###About MyRenderer"},
    {"Close", "关闭###Close"},
    {"Import diagnostics", "导入诊断###Import diagnostics"},
    {"Enable animation", "启用动画###Enable animation"},
    {"Play", "播放###Play"},
    {"Shader hot reload", "着色器热重载###Shader hot reload"},
    {"Half resolution", "云半分辨率###Half resolution"},
    {"Cloud temporal accumulation", "云时间累积###Cloud temporal accumulation"},
    {"Cloud shadows", "云阴影###Cloud shadows"},
    {"Cloud god rays", "云隙光束###Cloud god rays"},
    {"Cloud determinism", "确定性云渲染###Cloud determinism"},
    {"Cloud ray strength", "光束强度###Cloud ray strength"},
    {"Shadow mapping", "阴影映射###Shadow mapping"},
    {"Skybox", "天空盒###Skybox"},
    {"Normal mapping", "法线贴图###Normal mapping"},
    {"Background", "背景颜色###Background"},
    {"Direction", "方向###Direction"},
    {"Field of view", "视野角度###Field of view"},
    {"Far clip distance", "远裁剪距离###Far clip distance"},
    {"Local light stress", "局部光源压力测试###Local light stress"},
    {"Instance submission stress", "实例提交压力测试###Instance submission stress"},
    {"Glass feature toggles", "玻璃功能开关###Glass feature toggles"},
    {"Prism spectrum", "棱镜光谱###Prism spectrum"},
    {"Optical path details", "光路详情###Optical path details"},
  };
  if (!chinese || !key) return key;
  const auto it = values.find(key); return it == values.end() ? key : it->second.c_str();
}
inline void tooltip(const char* key) {
  if (!chineseFontAvailable || !ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) return;
  static const std::unordered_map<std::string, std::string> tips {
    {"ACES tone mapping", "将 HDR 亮度压缩到显示范围，同时保留亮部层次。"},
    {"Animate caustics", "让焦散图案随时间变化。"},
    {"Animation time", "当前动画在时间轴上的播放位置，单位为秒。"},
    {"Attenuation color", "光穿过玻璃后保留下来的颜色。"},
    {"Attenuation distance", "光衰减的参考距离，越小吸收越强。"},
    {"Auto rotate prism", "持续旋转棱镜以观察光谱变化。"},
    {"Beam direction", "入射光束在场景中的方向。"},
    {"Beam intensity", "入射光束的能量强度。"},
    {"Beam width", "光束的横向宽度。"},
    {"Bloom contribution", "光谱光束对泛光效果的贡献比例。"},
    {"CPU frustum culling", "在 CPU 上排除相机视野外的实例，减少绘制工作。"},
    {"Caustics direction", "焦散在接收平面上的偏移方向。"},
    {"Caustics mode", "选择投影焦散或基于光空间的焦散计算方式。"},
    {"Caustics scale", "焦散图案的空间尺度。"},
    {"Caustics sharpness", "焦散亮斑的集中程度。"},
    {"Caustics strength", "焦散叠加到接收表面的亮度。"},
    {"Central IOR", "参考波长的折射率，影响玻璃的折射角度。"},
    {"Colored transmission shadows", "让穿过透明材质的阴影呈现透射颜色。"},
    {"Dispersion override", "覆盖材质色散强度，控制不同波长的分离程度。"},
    {"Edge softness", "光束边缘的渐变宽度。"},
    {"Enable 2,500-instance scene", "切换到 2500 个实例的性能测试预设，会重置场景。"},
    {"Enable stress scene", "切换局部光源压力测试预设，会重置场景。"},
    {"G-buffer debug", "查看延迟渲染中间缓冲，便于检查材质和法线。"},
    {"GPU instancing / batching", "合并共享几何的绘制提交，减少 CPU 开销。"},
    {"Glass debug view", "查看折射、厚度等玻璃渲染中间结果。"},
    {"Glass preset", "选择预设的玻璃光学参数。"},
    {"Glass roughness", "玻璃表面粗糙度，影响反射高光和透射效果。"},
    {"HDR caustics", "启用高动态范围的聚光亮斑效果。"},
    {"Local light tier", "选择压力测试的局部光源数量档位。"},
    {"Lock hero camera", "固定演示相机，防止鼠标改变视角。"},
    {"Opaque render path", "选择前向渲染或延迟渲染路径。"},
    {"Optical path debug", "显示棱镜入射、折射和出射光路。"},
    {"Optical preset", "选择光学玻璃参数组合。"},
    {"Projected-size LOD", "按物体屏幕投影大小选择几何细节层级。"},
    {"Refraction scale", "控制屏幕空间折射采样的偏移尺度。"},
    {"Skinning debug", "显示蒙皮权重或关节等调试信息。"},
    {"Spectral beam ribbons", "显示按波长采样生成的彩色光束条带。"},
    {"Spectral samples", "参与光谱计算的波长数量；越多越平滑，开销越大。"},
    {"Spectrum mode", "选择光谱颜色的展示方式。"},
    {"TAA debug", "查看时域抗锯齿中间结果。"},
    {"TAA history weight", "历史帧混合权重；越大越稳定，也更容易出现拖影。"},
    {"Two-interface refraction", "分别计算进入和离开玻璃表面的折射。"},
    {"Volume glass material override", "使用全局体积玻璃参数覆盖材质设置。"},
    {"Volume thickness scale", "几何厚度的倍率，影响吸收与透射。"},
    {"White point", "调节光谱合成的白点参考。"},
    {"XYZ axes + gizmo", "显示世界坐标轴与视角方向标记。"},
    {"Position", "对象相对父对象的位置；无父对象时为世界坐标。"},
    {"Rotation", "对象绕 X、Y、Z 轴旋转的角度，单位为度。"},
    {"Scale", "对象沿各轴的缩放比例。"},
    {"Parent", "选择父对象后，当前对象会继承父对象的变换。"},
    {"Ambient", "环境光强度，增大会提亮背光区域。"},
    {"Diffuse", "漫反射强度，控制表面接受直接光的亮度。"},
    {"Specular", "镜面反射强度，控制高光亮度。"},
    {"Shininess", "高光指数，越大则高光越集中。"},
    {"Exposure", "曝光倍率，增大会提亮整个画面。"},
    {"Bloom", "使明亮区域向周围扩散，形成泛光。"},
    {"Bloom threshold", "泛光亮度阈值；仅高于阈值的像素产生泛光。"},
    {"Bloom intensity", "控制泛光叠加到最终画面的强度。"},
    {"Field of view", "透视相机视野角度，越大可看到越宽的范围。"},
    {"Far clip distance", "决定远处海面何时被裁掉；开放海域可提高到 1600，但会降低远处深度精度。"},
    {"Surface roughness", "控制环境反射的模糊程度。"},
    {"Sky reflection", "控制 HDRI 天空盒在海面上的反射强度。"},
    {"Fine ripples", "控制两层细波法线强度；只改变反射，不改变波浪几何。"},
    {"Sun glint", "控制方向光在波峰形成的高光。"},
    {"Deep water", "没有海底物体时，以深水底色代替天空折射；旧场景默认关闭。"},
    {"SSAO", "屏幕空间环境光遮蔽，用于增强接触处和凹陷处的暗部。"},
    {"SSAO radius", "环境遮蔽采样半径，决定暗部影响范围。"},
    {"SSAO bias", "遮蔽深度偏移，可减少表面错误自遮蔽。"},
    {"SSAO strength", "环境遮蔽暗部强度。"},
    {"Temporal AA", "混合历史帧以减轻锯齿；运动时可能出现拖影。"},
    {"MSAA", "多重采样抗锯齿，增加采样数会提高显存与 GPU 开销。"},
    {"VSync", "使呈现与显示器刷新同步，减少画面撕裂。"},
    {"Glass transmission", "启用穿过玻璃的光线透射。"},
    {"Dispersion", "不同波长折射程度不同，形成彩色分离。"},
    {"Refraction steps", "屏幕空间折射步进次数；越高通常越精细，开销也越大。"},
    {"Environment", "环境贴图的光照强度。"},
    {"Metallic-roughness PBR", "使用金属度与粗糙度描述材质的物理光照模型。"},
    {"Image-based lighting", "使用环境贴图提供间接漫反射和镜面反射。"},
    {"Geometric glass thickness", "根据几何深度估计玻璃厚度，影响折射与吸收。"},
    {"Wireframe", "以三角形边线显示模型。"},
    {"Back-face culling", "跳过背向相机的三角形，可提高性能。"},
    {"Normal mapping", "使用法线贴图改变表面光照细节。"},
    {"Direction", "平行光照射方向，以三维向量表示。"},
    {"Playback speed", "动画播放速度倍率。"},
    {"Base color tint", "全局基础颜色调制，会影响场景中的渲染效果。"},
    {"Entity tint", "仅调整当前选中对象的颜色调制。"},
    {"Visibility", "控制当前对象是否参与视口和最终渲染。"},
    {"Casts shadow", "控制当前对象是否写入阴影贴图。"},
    {"Ground offset", "地面在竖直方向的位置。"},
    {"Frame", "将相机聚焦到当前选中对象。"},
    {"Auto rotate", "持续旋转主模型，用于观察各个角度。"},
  };
  std::string name(key); const auto marker = name.find("###"); if (marker != std::string::npos) name = name.substr(marker + 3);
  const auto it = tips.find(name);
  ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
  if (it != tips.end()) ImGui::TextUnformatted(it->second.c_str());
  else ImGui::Text("%s：调整此项以控制对应的渲染效果；灰色表示当前模式不可用。", name.c_str());
  ImGui::PopTextWrapPos(); ImGui::EndTooltip();
}
inline bool section(const char* name, bool defaultOpen = false) {
  const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
    | ImGuiTreeNodeFlags_FramePadding
    | (defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
  return ImGui::CollapsingHeader(label(name), flags);
}
inline bool toolbarToggle(const char* name, bool* value) {
  if (*value) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.34f, 0.58f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.43f, 0.72f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.29f, 0.51f, 1.0f));
  }
  const bool pressed = ImGui::SmallButton(label(name));
  if (*value) ImGui::PopStyleColor(3);
  if (pressed) *value = !*value;
  tooltip(name);
  return pressed;
}
template<class Draw> inline bool propertyRow(const char* name, Draw&& draw) {
  const char* translated = label(name);
  std::string visible = translated ? translated : "";
  if (const auto marker = visible.find("###"); marker != std::string::npos) visible.resize(marker);
  ImGui::PushID(name ? name : "Property");
  ImGui::PushID(static_cast<int>(ImGui::GetCursorPosY() * 10.0f));
  bool changed = false;
  if (ImGui::BeginTable(
      "##PropertyRow",
      2,
      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
        | ImGuiTableFlags_NoPadOuterX
    )) {
    const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.42f, 96.0f, 150.0f);
    ImGui::TableSetupColumn("##PropertyName", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("##PropertyValue", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", visible.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed = draw("##Value");
    tooltip(name);
    ImGui::EndTable();
  }
  ImGui::PopID();
  ImGui::PopID();
  return changed;
}
template<class... Args> inline bool Checkbox(const char* name, Args&&... args) {
  if (name && name[0] == '#' && name[1] == '#') {
    const bool changed = ImGui::Checkbox(name, std::forward<Args>(args)...);
    tooltip(name);
    return changed;
  }
  return propertyRow(name, [&](const char* id) { return ImGui::Checkbox(id, std::forward<Args>(args)...); });
}
template<class... Args> inline bool SliderFloat(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::SliderFloat(id, std::forward<Args>(args)...); }); }
template<class... Args> inline bool SliderFloat3(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::SliderFloat3(id, std::forward<Args>(args)...); }); }
template<class... Args> inline bool SliderInt(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::SliderInt(id, std::forward<Args>(args)...); }); }
template<class... Args> inline bool DragFloat(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::DragFloat(id, std::forward<Args>(args)...); }); }
template<class... Args> inline bool DragFloat3(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::DragFloat3(id, std::forward<Args>(args)...); }); }
template<class... Args> inline bool ColorEdit3(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::ColorEdit3(id, std::forward<Args>(args)...); }); }
template<class... Args> inline bool Combo(const char* name, Args&&... args) { return propertyRow(name, [&](const char* id) { return ImGui::Combo(id, std::forward<Args>(args)...); }); }
}
