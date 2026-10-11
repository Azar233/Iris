#include "StoredPluginFixture.h"
#include "app/RenderPluginPanel.h"
#include "app/EditorUi.h"
#include "scene/SceneDocument.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <fstream>
#include <array>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
class StoredPlugin final : public iris::RenderPlugin {
public:
    inline static GLuint lastPreparedTexture{0};
    explicit StoredPlugin(const iris::RenderPluginParameterSchema& schema) : schema_(schema) {}
    ~StoredPlugin() override {
        if (resourceTexture_) glDeleteTextures(1, &resourceTexture_);
        if (resourceFramebuffer_) glDeleteFramebuffers(1, &resourceFramebuffer_);
    }
    void prepareResources(const RendererSettings& settings, int, int) override {
        const auto entries = schema_.capture(settings);
        for (const auto& entry : entries) if (entry.value.type == ModuleParameterType::Asset && !entry.value.text.empty()) {
            std::ifstream input(std::filesystem::u8path(entry.value.text));
            std::array<float,4> color{0,0,0,1}; std::string extra;
            if (!(input >> color[0] >> color[1] >> color[2]) || (input >> extra))
                throw std::runtime_error("Invalid RGB resource");
            for (float channel : color) if (!std::isfinite(channel) || channel < 0 || channel > 1)
                throw std::runtime_error("Invalid resource channel");
            glGenTextures(1, &resourceTexture_); glBindTexture(GL_TEXTURE_2D, resourceTexture_);
            lastPreparedTexture=resourceTexture_;
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_FLOAT,color.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glGenFramebuffers(1,&resourceFramebuffer_); glBindFramebuffer(GL_FRAMEBUFFER,resourceFramebuffer_);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,resourceTexture_,0);
            require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Resource FBO incomplete");
        }
    }
    void renderFrame(const iris::RenderPluginFrame& frame) override {
        if (resourceTexture_) {
            frame.target.resize(frame.width,frame.height,1); frame.target.bindFinal();
            glBindFramebuffer(GL_READ_FRAMEBUFFER,resourceFramebuffer_);
            glBlitFramebuffer(0,0,1,1,0,0,frame.width,frame.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            frame.target.bindFinal(); return;
        }
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
    GLuint resourceTexture_{0}, resourceFramebuffer_{0};
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
        std::vector<iris::RenderPluginDescriptor> descriptors = {
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
        auto resourceSchema = storedPluginSchema("test.resource");
        resourceSchema.version = 2;
        resourceSchema.metadata.registerAsset("image", "Image resource path", ".rgb");
        catalog.add(resourceSchema);
        const auto* resource = catalog.find("test.resource");
        const auto imagePath = output/"input.rgb";
        bool mutateDuringPrepare=false;
        registry.add({resource->pluginId,iris::renderPluginApiVersion,{},"MIT",{},resource},
            [&,resource](const std::filesystem::path&) {
                auto result=std::make_unique<StoredPlugin>(*resource);
                if(mutateDuringPrepare) { std::ofstream file(imagePath); file<<"0.8 0.1 0.2"; }
                return result;
            });
        { std::ofstream file(imagePath); file << "0.2 0.9 0.1"; }
        ModuleParameterOverride image; image.id="image"; image.value.type=ModuleParameterType::Asset;
        image.value.text=std::filesystem::absolute(imagePath).generic_u8string();
        auto resourceSettings=settings;
        require(resource->apply(resourceSettings,{image},history,error),"Resource parameter apply failed");
        auto replacement=registry.prepareReplacement(resource->pluginId,{}, {},resourceSettings,320,180);
        plugin.swap(replacement); settings=resourceSettings; replacement.reset();
        const auto resourcePixels=pixels();
        require(resourcePixels!=baseline,"Prepared texture was not consumed by GPU");
        require(target.savePng(output/"resource.png",error),"Resource image save failed");
        const auto* retainedOwner=plugin.get();
        const auto failPreparation = [&](auto work) {
            const auto previousTexture=StoredPlugin::lastPreparedTexture;
            bool rejected=false; try { work(); } catch(const std::exception&) { rejected=true; }
            require(rejected && plugin.get()==retainedOwner && pixels()==resourcePixels,"Failed candidate escaped publication");
            if (StoredPlugin::lastPreparedTexture!=previousTexture)
                require(glIsTexture(StoredPlugin::lastPreparedTexture)==GL_FALSE,"Rejected candidate texture leaked");
        };
        auto missing=resourceSettings;
        image.value.text=(output/"missing.rgb").generic_u8string();
        require(resource->apply(missing,{image},history,error),"Missing resource path schema rejected");
        failPreparation([&]{registry.prepareReplacement(resource->pluginId,{}, {},missing,320,180);});
        { std::ofstream file(imagePath); file << "broken data"; }
        failPreparation([&]{registry.prepareReplacement(resource->pluginId,{}, {},resourceSettings,320,180);});
        { std::ofstream file(imagePath); file << "0.2 0.9 0.1"; }
        int currentCalls=0;
        failPreparation([&]{registry.prepareReplacement(resource->pluginId,{}, {},resourceSettings,320,180,
            [&]{return ++currentCalls==1;});});
        currentCalls=0;
        failPreparation([&]{registry.prepareReplacement(resource->pluginId,{}, {},resourceSettings,320,180,
            [&]{ if(++currentCalls==2) throw std::runtime_error("stale generation"); return true; });});
        mutateDuringPrepare=true;
        failPreparation([&]{registry.prepareReplacement(resource->pluginId,{}, {},resourceSettings,320,180);});
        mutateDuringPrepare=false;
        registry.add({"test.changed",iris::renderPluginApiVersion,{},"MIT",{},nullptr},
            [&](const std::filesystem::path&) -> std::unique_ptr<iris::RenderPlugin> { throw std::runtime_error("factory failed"); });
        failPreparation([&]{registry.prepareReplacement("test.changed",{}, {},settings,320,180);});
        { std::ofstream file(imagePath); file << "0.8 0.1 0.2"; }
        replacement=registry.prepareReplacement(resource->pluginId,{}, {},resourceSettings,320,180);
        plugin.swap(replacement); replacement.reset();
        require(pixels()!=resourcePixels,"Repaired resource did not publish a new texture");
        SceneDocument resourceScene; resourceScene.renderer=resourceSettings;
        require(saveSceneDocument(output/"resource.myscene",resourceScene,error,catalog),"Resource Scene save failed");
        SceneDocument resourceRestored;
        require(loadSceneDocument(output/"resource.myscene",resourceRestored,error,catalog),"Resource Scene load failed");
        schema=resource; descriptors.front()={resource->pluginId,iris::renderPluginApiVersion,{},"MIT",{},resource};
        status.clear(); ui(); ui(); require(controls.size()==6,"Asset schema did not generate path control");
        const auto assetControl=controls.back();
        io.AddMousePosEvent(assetControl.x,assetControl.y); ui(); io.AddMouseButtonEvent(0,true); ui(); ui();
        io.AddMouseButtonEvent(0,false); ui(); ui();
        screenshot("asset-before.png");
        io.AddKeyEvent(ImGuiMod_Ctrl,true); ui(); io.AddKeyEvent(ImGuiKey_A,true); ui(); ui();
        io.AddKeyEvent(ImGuiKey_A,false); ui(); io.AddKeyEvent(ImGuiMod_Ctrl,false); ui();
        io.AddInputCharactersUTF8((output/"missing.rgb").generic_u8string().c_str()); ui(); ui();
        io.AddKeyEvent(ImGuiKey_Enter,true); command=ui(); if (!command) command=ui();
        io.AddKeyEvent(ImGuiKey_Enter,false); ui();
        require(command && command->moduleParameter.type==static_cast<int>(ModuleParameterType::Asset)
            && command->pluginParameterId=="image","Actual Asset path edit did not emit command");
        require(command->moduleParameter.text==(output/"missing.rgb").generic_u8string(),"Asset input text was not replaced");
        ModuleParameterOverride editedAsset; editedAsset.id=command->pluginParameterId;
        editedAsset.value.type=ModuleParameterType::Asset; editedAsset.value.text=command->moduleParameter.text;
        auto editedSettings=settings;
        require(iris::setPluginParameter(registry,editedSettings,resource->pluginId,editedAsset,history,error),"Asset command rejected before resource preparation");
        const auto retainedPixels=pixels(); const auto* retainedResource=plugin.get();
        bool editRejected=false;
        try { registry.prepareReplacement(resource->pluginId,{}, {},editedSettings,320,180); }
        catch(const std::exception&) { editRejected=true; }
        require(editRejected && plugin.get()==retainedResource && pixels()==retainedPixels,"Asset UI failure changed live resources");
        status="Resource replacement rejected; prior instance retained"; ui(); screenshot("asset-error.png");
        std::cout<<"Asset GPU texture / candidate failure retention / cancel-stale / recovery / Scene PASS\n";
        std::cout<<"Stored plugin GPU / commands / Scene / recreation / rejection / double-size UI PASS\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return result;
}
