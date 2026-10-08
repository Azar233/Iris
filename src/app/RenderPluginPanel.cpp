#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
namespace iris {
std::optional<EditorCommand> drawRenderPluginPanel(const RenderPluginRegistry& registry,
    const std::vector<RenderPluginDescriptor>& catalog,const RenderPluginConfiguration& config,
    const std::string& required,std::vector<PluginControlBounds>* controls){
    std::optional<EditorCommand> change;
    if(controls)controls->clear();
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
        ImGui::PopID();
    }
    return change;
}
}
