#include "StoredPluginFixture.h"
#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
#include "scene/SceneDocument.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
class StoredPlugin final : public iris::RenderPlugin {
public:
    explicit StoredPlugin(const iris::RenderPluginParameterSchema& schema) : schema_(schema) {}
    void renderFrame(const iris::RenderPluginFrame& frame) override {
        auto values = schema_.metadata;
        std::string error;
        require(values.applyOverrides(frame.parameterValues(schema_), error), "Frame parameter values rejected");
        auto color = values.colorValue("tint", glm::vec3(0)) * values.floatValue("gain", 1);
        if (values.enumLabel("preset") == "inverted") color = glm::vec3(1) - color;
        if (!values.boolValue("enabled", true)) color = glm::vec3(0);
        color *= static_cast<float>(values.intValue("bands", 4)) / 8.0f;
        frame.target.resize(frame.width, frame.height, 1);
        frame.target.bindFinal(); glViewport(0, 0, frame.width, frame.height);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
        glClearColor(color.x, color.y, color.z, 1); glClear(GL_COLOR_BUFFER_BIT);
    }
    void invalidateHistory() override {}
    iris::RenderPluginFrameInfo frameInfo() const override { return {}; }
private:
    const iris::RenderPluginParameterSchema& schema_;
};
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(1440, 900, "Stored plugin acceptance", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DeltaTime = 1.0f/60;
    EditorUi::chinese = false;
    ImGui_ImplOpenGL3_Init("#version 330 core");
    int result = 0;
    try {
        const auto output = std::filesystem::path(MYRENDERER_STORED_PLUGIN_OUTPUT);
        std::filesystem::create_directories(output);
        auto catalog = iris::builtinRenderPluginParameterCatalog(); catalog.add(storedPluginSchema());
        const auto* schema = catalog.find("test.stored");
        iris::RenderPluginRegistry registry;
        const std::vector<iris::RenderPluginDescriptor> descriptors = {
            {schema->pluginId, iris::renderPluginApiVersion, {}, "MIT (test fixture)", {}, schema}};
        registry.add(descriptors.front(), [schema](const std::filesystem::path&) { return std::make_unique<StoredPlugin>(*schema); });
        RendererSettings settings; Camera camera; RenderTarget target;
        auto plugin = registry.create(schema->pluginId, {}, {});
        const auto pixels = [&]() {
            plugin->renderFrame({target, camera, settings, 320, 180, 0, 0});
            std::vector<unsigned char> bytes(320U*180U*4U);
            glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
            require(glGetError() == GL_NO_ERROR, "Stored plugin GL error");
            return bytes;
        };
        const auto baseline = pixels(); std::string error;
        require(target.savePng(output/"before.png", error), "Baseline PNG failed");
        std::vector<iris::PluginControlBounds> controls;
        int width = 1440, height = 900;
        std::string status;
        const auto ui = [&](bool available = true) -> std::optional<EditorCommand> {
            glfwSetWindowSize(window, width, height);
            int framebufferWidth = 0, framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
            io.DisplayFramebufferScale = ImVec2(static_cast<float>(framebufferWidth)/width, static_cast<float>(framebufferHeight)/height);
            ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(12, 12)); ImGui::SetNextWindowSize(ImVec2(260, static_cast<float>(height-24)));
            ImGui::Begin("Registered plugin parameters", nullptr, ImGuiWindowFlags_NoSavedSettings);
            iris::RenderPluginRegistry missing;
            auto command = iris::drawRenderPluginPanel(available ? registry : missing, descriptors, settings.renderPlugins,
                schema->pluginId, nullptr, &settings, &controls);
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::End(); ImGui::Render(); target.unbind();
            glViewport(0, 0, framebufferWidth, framebufferHeight); glClearColor(0.08f, 0.09f, 0.1f, 1);
            glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glFinish();
            require(glGetError() == GL_NO_ERROR, "Parameter UI GL error");
            return command;
        };
        const auto screenshot = [&](const std::string& name) {
            int w=0,h=0; glfwGetFramebufferSize(window,&w,&h);
            require(RenderTarget::saveDefaultFramebufferPng(output/name,w,h,error), "UI PNG failed");
        };
        for (const auto size : {ImVec2(1440,900), ImVec2(1100,680)}) {
            width=static_cast<int>(size.x); height=static_cast<int>(size.y);
            ui(); ui(); require(controls.size()==5, "Unbound schema did not generate typed controls");
            const auto button=controls.front();
            io.AddFocusEvent(true); io.AddMousePosEvent(button.x,button.y); io.AddMouseButtonEvent(0,true); ui();
            io.AddMouseButtonEvent(0,false); auto command=ui();
            require(command && command->pluginParameterId=="enabled", "Unbound Bool input did not emit command");
            ModuleParameterOverride edit; edit.id=command->pluginParameterId;
            edit.value.type=ModuleParameterType::Bool; edit.value.boolean=command->moduleParameter.boolean;
            bool history=false;
            require(iris::setPluginParameter(registry,settings,schema->pluginId,edit,history,error) && history,
                "Unbound UI command did not commit/history policy lost");
            const auto changed=pixels(); require(changed!=baseline,"Stored parameter did not change GPU output");
            require(target.savePng(output/"changed.png",error),"Changed PNG failed");
            SceneDocument scene; scene.renderer=settings;
            require(saveSceneDocument(output/"scene.myscene",scene,error,catalog),"Custom Scene save failed");
            SceneDocument restored; require(loadSceneDocument(output/"scene.myscene",restored,error,catalog),"Custom Scene load failed");
            settings=restored.renderer; plugin.reset(); plugin=registry.create(schema->pluginId,{},{});
            require(pixels()==changed,"Plugin recreation/Scene roundtrip changed GPU output");
            ModuleParameterOverride bad; bad.id="gain"; bad.value.type=ModuleParameterType::Float; bad.value.number=9;
            require(!iris::setPluginParameter(registry,settings,schema->pluginId,bad,history,error) && !history
                && pixels()==changed,"Rejected stored parameter changed GPU output");
            status=error; ui(); screenshot("error-"+std::to_string(width)+".png");
            status="Scene roundtrip and GPU output retained"; ui(); screenshot("stored-"+std::to_string(width)+".png");
            ui(false); ui(false); screenshot("unavailable-"+std::to_string(width)+".png");
            const auto unavailable=controls.front();
            io.AddMousePosEvent(unavailable.x,unavailable.y); io.AddMouseButtonEvent(0,true); ui(false);
            io.AddMouseButtonEvent(0,false); require(!ui(false),"Unavailable stored control was interactive");
            edit.value.boolean=true;
            require(iris::setPluginParameter(registry,settings,schema->pluginId,edit,history,error)
                && pixels()==baseline,"Restoring schema default changed GPU output");
            status.clear();
        }
        // A Float drag also follows the same production command and GPU path.
        ui(); const auto gain=controls[1];
        io.AddMousePosEvent(gain.x,gain.y); io.AddMouseButtonEvent(0,true); ui();
        io.AddMousePosEvent(gain.x+40,gain.y); auto command=ui();
        require(command && command->pluginParameterId=="gain" && command->moduleParameter.number>1,"Unbound Float drag failed");
        io.AddMouseButtonEvent(0,false); ui();
        ModuleParameterOverride edit; edit.id="gain"; edit.value.type=ModuleParameterType::Float; edit.value.number=command->moduleParameter.number;
        bool history=false;
        require(iris::setPluginParameter(registry,settings,schema->pluginId,edit,history,error)
            && pixels()!=baseline,"Float command did not affect plugin output");
        std::cout<<"Stored plugin GPU / commands / Scene / recreation / rejection / double-size UI PASS\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return result;
}
