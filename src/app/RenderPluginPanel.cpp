#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
namespace iris {
std::optional<EditorCommand> drawRenderPluginPanel(const RenderPluginRegistry& registry,
    const std::vector<RenderPluginDescriptor>& catalog,const RenderPluginConfiguration& config,
    const std::string& required,std::vector<PluginControlBounds>* controls,
    const RendererSettings* settings, std::vector<PluginControlBounds>* parameterControls){
    std::optional<EditorCommand> change;
    if(controls)controls->clear();
    if(parameterControls)parameterControls->clear();
    ImGui::TextWrapped("%s",EditorUi::chinese?"当前场景插件配置，随场景保存。":"Plugin configuration is saved with this scene.");
    for(const auto& descriptor:catalog){
        ImGui::PushID(descriptor.id.c_str());ImGui::Separator();
        ImGui::TextWrapped("%s",descriptor.id.c_str());
        const bool available=registry.contains(descriptor.id);
        const bool needed=required==descriptor.id;
        bool enabled=available&&pluginEnabled(config,descriptor.id);
        ImGui::TextWrapped("%s",!available?(EditorUi::chinese?"未编译，无法启用":"Not built / unavailable"):
            needed?(EditorUi::chinese?"当前管线正在使用":"In use by current pipeline"):
            (EditorUi::chinese?"当前管线未使用":"Not used by current pipeline"));
        ImGui::BeginDisabled(!available||needed);
        const bool toggled=EditorUi::propertyRow(EditorUi::chinese?"启用":"Enabled",[&](const char* id){
            const bool clicked=ImGui::Checkbox(id,&enabled);
            if(controls){const auto a=ImGui::GetItemRectMin();const auto b=ImGui::GetItemRectMax();controls->push_back({descriptor.id,(a.x+b.x)*.5f,(a.y+b.y)*.5f});}
            return clicked;
        });
        if(toggled){
            change=EditorCommand{EditorCommandType::SetRenderPluginEnabled};change->text=descriptor.id;change->flag=enabled;
        }
        ImGui::EndDisabled();
        if(needed)ImGui::TextWrapped("%s",EditorUi::chinese?"此项为当前管线必需。切换管线后可停用。":"Required by this pipeline. Switch pipelines before disabling.");
        ImGui::TextWrapped("API %d | %s",descriptor.apiVersion,descriptor.license.c_str());
        for(const auto& service:descriptor.requiredServices)ImGui::TextWrapped("%s: %s",EditorUi::chinese?"依赖":"Requires",service.c_str());
        if (settings && descriptor.parameters && ImGui::CollapsingHeader("Parameters",
                needed ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
            ImGui::BeginDisabled(!available || !enabled);
            const auto values = descriptor.parameters->capture(*settings);
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
                            changed = ImGui::SliderInt(widgetId, &value.integer,
                                static_cast<int>(metadata->minimum), static_cast<int>(metadata->maximum)); break;
                        case ModuleParameterType::Color: changed = ImGui::ColorEdit3(widgetId, &value.color.x); break;
                        case ModuleParameterType::Enum: {
                            std::vector<const char*> labels;
                            for (const auto& label : metadata->enumLabels) labels.push_back(label.c_str());
                            changed = ImGui::Combo(widgetId, &value.integer, labels.data(), static_cast<int>(labels.size()));
                            if (changed) value.text = metadata->enumLabels[static_cast<std::size_t>(value.integer)];
                            break;
                        }
                        case ModuleParameterType::Asset: ImGui::TextDisabled("Resource parameters unavailable"); break;
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
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
    return change;
}
}
