#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
#include "render/Renderer.h"
#include <algorithm>
namespace iris {
namespace {
const char* pluginName(const std::string& id) {
    if (id == enscapePluginId) return EditorUi::chinese ? "Enscape 场景" : "Enscape scene";
    if (id == postProcessPluginId) return EditorUi::chinese ? "后期处理" : "Post processing";
    if (id == nprPluginId) return EditorUi::chinese ? "NPR 风格化" : "NPR stylized";
    if (id == waterPluginId) return EditorUi::chinese ? "原生海面" : "Native water";
    return id.c_str();
}
void pluginIcon() {
    const float size = ImGui::GetFrameHeight();
    const auto pos = ImGui::GetCursorScreenPos();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRect(ImVec2(pos.x+4, pos.y+5), ImVec2(pos.x+size-4, pos.y+size-5), color, 3.0f);
    draw->AddLine(ImVec2(pos.x+size*.4f, pos.y+2), ImVec2(pos.x+size*.4f, pos.y+5), color);
    draw->AddLine(ImVec2(pos.x+size*.6f, pos.y+2), ImVec2(pos.x+size*.6f, pos.y+5), color);
    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine();
}
}
std::optional<EditorCommand> drawRenderPluginPanel(const RenderPluginRegistry& registry,
    const std::vector<RenderPluginDescriptor>& catalog,const RenderPluginConfiguration& config,
    const std::string& required,std::vector<PluginControlBounds>* controls,
    const RendererSettings* settings) {
    std::optional<EditorCommand> change;
    if (controls) controls->clear();
    ImGui::TextWrapped("%s", EditorUi::chinese ? "启用后在渲染设置中调整参数" : "Configure enabled plugins in Render settings");
    for (const auto& descriptor : catalog) {
        ImGui::PushID(descriptor.id.c_str());
        ImGui::Separator();
        const bool available = registry.contains(descriptor.id);
        const bool needed = required == descriptor.id || (settings && ((descriptor.id == nprPluginId && nprRequired(*settings))
            || (descriptor.id == waterPluginId && waterRequired(*settings))));
        bool enabled = available && pluginEnabled(config, descriptor.id);
        pluginIcon();
        ImGui::BeginDisabled(!available || needed);
        const bool toggled = EditorUi::propertyRow(pluginName(descriptor.id), [&](const char* id) {
            const bool clicked = ImGui::Checkbox(id, &enabled);
            if (controls) {
                const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                controls->push_back({descriptor.id, (a.x+b.x)*.5f, (a.y+b.y)*.5f});
            }
            return clicked;
        });
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(descriptor.id.c_str());
            if (!available) ImGui::TextUnformatted(EditorUi::chinese ? "未编译，无法启用" : "Not built / unavailable");
            else if (needed) ImGui::TextUnformatted(EditorUi::chinese ? "当前管线必需，切换管线后可停用" : "Required by this pipeline; switch pipelines before disabling");
            ImGui::Text("API %d | %s", descriptor.apiVersion, descriptor.license.c_str());
            for (const auto& service : descriptor.requiredServices) ImGui::Text("%s", service.c_str());
            ImGui::EndTooltip();
        }
        if (toggled) {
            change = EditorCommand{EditorCommandType::SetRenderPluginEnabled};
            change->text = descriptor.id; change->flag = enabled;
        }
        if (!available || needed) {
            ImGui::TextDisabled("%s", !available ? (EditorUi::chinese ? "不可用" : "Unavailable")
                : (EditorUi::chinese ? "当前管线必需" : "Required by pipeline"));
        }
        ImGui::PopID();
    }
    return change;
}
std::optional<EditorCommand> drawRenderPluginSettings(const RenderPluginRegistry& registry,
    const std::vector<RenderPluginDescriptor>& catalog, const RendererSettings& settings,
    std::vector<PluginControlBounds>* parameterControls, std::vector<PluginControlBounds>* resourceControls) {
    std::optional<EditorCommand> change;
    if (parameterControls) parameterControls->clear();
    if (resourceControls) resourceControls->clear();
    for (const auto& descriptor : catalog) {
        if (!registry.contains(descriptor.id) || !pluginEnabled(settings.renderPlugins, descriptor.id)) continue;
        ImGui::PushID(descriptor.id.c_str());
        const bool active = descriptor.id == enscapePluginId ? settings.enscapeCubeShaderEnabled
            : descriptor.id == postProcessPluginId ? !settings.enscapeCubeShaderEnabled
            : descriptor.id == nprPluginId ? nprRequired(settings)
            : descriptor.id == waterPluginId ? waterRequired(settings) : true;
        if (ImGui::CollapsingHeader(pluginName(descriptor.id), active ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
            const auto values = descriptor.parameters ? descriptor.parameters->capture(settings) : std::vector<ModuleParameterOverride>{};
            for (const auto& entry : values) {
                const auto* metadata = descriptor.parameters->metadata.descriptor(entry.id);
                if (!metadata) continue;
                ImGui::PushID(entry.id.c_str());
                auto value = entry.value;
                const bool edited = EditorUi::propertyRow(metadata->displayName.c_str(), [&](const char* widgetId) {
                    bool changed = false;
                    switch (metadata->type) {
                        case ModuleParameterType::Bool: changed = ImGui::Checkbox(widgetId, &value.boolean); break;
                        case ModuleParameterType::Float:
                            changed = ImGui::DragFloat(widgetId, &value.number, 0.01f,
                                static_cast<float>(metadata->minimum), static_cast<float>(metadata->maximum), "%.3f"); break;
                        case ModuleParameterType::Int:
                            if (metadata->maximum - metadata->minimum > 100000)
                                changed = ImGui::DragInt(widgetId, &value.integer, 1.0f,
                                    static_cast<int>(metadata->minimum), static_cast<int>(metadata->maximum));
                            else changed = ImGui::SliderInt(widgetId, &value.integer,
                                static_cast<int>(metadata->minimum), static_cast<int>(metadata->maximum)); break;
                        case ModuleParameterType::Color: changed = ImGui::ColorEdit3(widgetId, &value.color.x); break;
                        case ModuleParameterType::Enum: {
                            std::vector<const char*> labels;
                            for (const auto& label : metadata->enumLabels) labels.push_back(label.c_str());
                            changed = ImGui::Combo(widgetId, &value.integer, labels.data(), static_cast<int>(labels.size()));
                            if (changed) value.text = metadata->enumLabels[static_cast<std::size_t>(value.integer)];
                            break;
                        }
                        case ModuleParameterType::Asset: {
                            std::vector<char> path(4097, 0);
                            std::copy_n(value.text.data(), std::min(value.text.size(), path.size()-1), path.data());
                            changed = ImGui::InputText(widgetId, path.data(), path.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                            if (changed) value.text = path.data();
                            break;
                        }
                    }
                    if (parameterControls) {
                        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                        parameterControls->push_back({descriptor.id + "/" + entry.id, (a.x+b.x)*0.5f, (a.y+b.y)*0.5f});
                    }
                    return changed;
                });
                if (ImGui::IsItemHovered() && !metadata->tooltip.empty()) ImGui::SetTooltip("%s", metadata->tooltip.c_str());
                if (edited) {
                    change = EditorCommand{EditorCommandType::SetRenderPluginParameter};
                    change->text = descriptor.id; change->pluginParameterId = entry.id;
                    change->moduleParameter.type = static_cast<int>(value.type);
                    change->moduleParameter.boolean = value.boolean;
                    change->moduleParameter.integer = value.integer;
                    change->moduleParameter.number = value.number;
                    change->moduleParameter.text = value.text;
                    change->color = {value.color.x, value.color.y, value.color.z};
                }
                ImGui::PopID();
            }
            if (ImGui::Button(EditorUi::chinese ? "重建插件资源" : "Rebuild plugin resources")) {
                change = EditorCommand{EditorCommandType::RebuildRenderPluginResources};
                change->text = descriptor.id;
            }
            if (resourceControls) {
                const auto a=ImGui::GetItemRectMin(), b=ImGui::GetItemRectMax();
                resourceControls->push_back({descriptor.id,(a.x+b.x)*0.5f,(a.y+b.y)*0.5f});
            }
        }
        ImGui::PopID();
    }
    return change;
}
}
