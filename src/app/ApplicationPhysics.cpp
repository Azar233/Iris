#include "app/Application.h"
#include "app/EditorUi.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

void Application::interactPhysicsControl(bool valueChanged) {
    physicsUiChanged_ = physicsUiChanged_ || valueChanged;
    if (physicsUiPhase_ == 2 && physicsUiChanged_) {
        physicsUiPhase_ = 3;
        std::cout << "Physics ImGui control interaction: PASS\n";
    }
    if (!ImGui::IsItemVisible() || ImGui::GetFrameCount() < 8) return;
    auto& io = ImGui::GetIO();
    if (physicsUiPhase_ == 0) {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        io.AddFocusEvent(true); io.AddMousePosEvent(a.x + (b.x-a.x)*0.7f, (a.y+b.y)*0.5f);
        io.AddMouseButtonEvent(0, true); physicsUiPhase_ = 1;
    } else if (physicsUiPhase_ == 1) {
        io.AddMouseButtonEvent(0, false); physicsUiPhase_ = 2;
    }
}

void Application::runPhysicsWorkbenchAcceptance() {
    const auto check = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    check(activeModuleId_ == rigidBodyModuleId && scene_.find(2), "Physics workbench requires the bundled fixture");
    const auto authoredHash = sceneContentHash(scene_);
    editorSession_.setFrameRange(0, 120); editorSession_.setFramesPerSecond(30);
    editorSession_.requestActivity(EditorActivity::Preview);
    editorSession_.requestPause(true); processEditorCommands();
    editorSession_.setFrame(0); updateModulePreview();
    const auto initial = moduleRuntime_.report().contentHash;
    editorSession_.request(EditorCommand{EditorCommandType::Step}); processEditorCommands(); updateModulePreview();
    check(editorSession_.paused() && moduleRuntime_.report().lastFrame == 1, "Physics Step did not pause at frame 1");
    check(moduleRuntime_.runtimeScene().scene().find(2)->motionHistoryValid, "Continuous physics step lost previous transform");
    editorSession_.request(EditorCommand{EditorCommandType::Reset}); processEditorCommands(); updateModulePreview();
    check(moduleRuntime_.report().lastFrame == 0 && moduleRuntime_.report().contentHash == initial,
        "Physics Reset did not restore authored initial pose");
    std::uint64_t expected = 0;
    for (const int displayFps : {30, 60, 144}) {
        editorSession_.setFrame(0); updateModulePreview();
        animationPlaying_ = true; animationTimeFixed_ = false;
        for (int display = 0; display <= displayFps * 4; ++display) {
            animationTimeSeconds_ = static_cast<float>(display) / displayFps;
            synchronizeModulePlayback(); updateModulePreview();
        }
        check(moduleRuntime_.report().lastFrame == 120, "Display FPS skipped physics end frame");
        if (expected == 0) expected = moduleRuntime_.report().contentHash;
        check(expected == moduleRuntime_.report().contentHash, "Display FPS changed physics trajectory");
    }
    editorSession_.setFrame(30); updateModulePreview();
    check(!moduleRuntime_.runtimeScene().scene().find(2)->motionHistoryValid, "Physics rewind kept stale motion history");
    check(sceneContentHash(scene_) == authoredHash, "Physics preview mutated the edit scene");
    animationPlaying_ = false; animationTimeFixed_ = true;
    animationTimeSeconds_ = static_cast<float>(editorSession_.timeSeconds());
    selectEntity(2); showPhysicsColliders_ = true;
    std::cout << "Physics workbench Step/Reset/30-60-144 display FPS/rewind/edit isolation: PASS\n";
}

void Application::advancePhysicsTemporalAcceptance() {
    if (!std::getenv("IRIS_PHYSICS_TEMPORAL_DIRECTORY")) return;
    const int phase = physicsTemporalFrame_;
    const int frame = phase <= 31 ? phase : phase == 32 ? 0 : phase == 33 ? 1
        : phase == 34 ? 90 : phase == 35 ? 0 : 1;
    if (phase == 35) {
        editorSession_.request(EditorCommand{EditorCommandType::Reset}); processEditorCommands();
    }
    editorSession_.setFrame(frame);
    animationTimeSeconds_ = static_cast<float>(editorSession_.timeSeconds());
    animationPlaying_ = false; animationTimeFixed_ = true;
    rendererSettings_.temporalAaEnabled = true;
}

void Application::capturePhysicsTemporalAcceptance() {
    const char* destination = std::getenv("IRIS_PHYSICS_TEMPORAL_DIRECTORY");
    if (!destination) return;
    const auto* entity = viewportScene().find(2);
    const int phase = physicsTemporalFrame_;
    const bool continuous = (phase > 0 && phase <= 31) || phase == 33 || phase >= 36;
    if (!entity || entity->motionHistoryValid != continuous)
        throw std::runtime_error("Physics rendered-frame motion history is incorrect");
    if (phase == 0 || phase >= 31) {
        const auto directory = std::filesystem::u8path(destination);
        std::filesystem::create_directories(directory);
        std::string error;
        if (!renderer_->saveScreenshot(directory / ("phase-" + std::to_string(phase) + ".png"), error))
            throw std::runtime_error(error);
    }
    if (++physicsTemporalFrame_ == 37) {
        std::cout << (editorSession_.backend()==EditorRenderBackend::VulkanRaster
            ? "Physics Vulkan shared transforms: continuous/rewind/jump/Reset PASS\n"
            : "Physics actual TAA renders: continuous/rewind/jump/Reset/history PASS\n");
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void Application::drawRigidBodyInspector(const SceneEntity& entity) {
    if (!EditorUi::section("Rigid body", true)) return;
    bool enabled = entity.rigidBody.has_value();
    const bool available = moduleRegistry_.contains(rigidBodyModuleId) && activeModuleId_ == rigidBodyModuleId;
    ImGui::BeginDisabled(!available);
    if (EditorUi::Checkbox("Enable rigid body", &enabled)) {
        EditorCommand command{EditorCommandType::SetEntityRigidBody, entity.id};
        if (enabled) command.rigidBody = RigidBodyComponent{};
        editorSession_.request(std::move(command));
        ImGui::EndDisabled(); return;
    }
    ImGui::EndDisabled();
    if (!available) {
        ImGui::TextWrapped("%s", EditorUi::chinese ? "请先启用 CPU Rigid Bodies 模块。" : "Select the CPU Rigid Bodies module first.");
    }
    if (!entity.rigidBody) return;
    auto body = *entity.rigidBody;
    int type = static_cast<int>(body.type), shape = static_cast<int>(body.shape);
    const char* types[]{"Static", "Dynamic"};
    const char* shapes[]{"Sphere", "Box", "Plane"};
    ImGui::BeginDisabled(body.shape == CollisionShape::Plane);
    bool changed = EditorUi::Combo("Body type", &type, types, 2);
    ImGui::EndDisabled();
    body.type = static_cast<RigidBodyType>(type);
    changed |= EditorUi::Combo("Collision shape", &shape, shapes, 3);
    body.shape = static_cast<CollisionShape>(shape);
    if (body.shape == CollisionShape::Plane) body.type = RigidBodyType::Static;
    if (body.type == RigidBodyType::Static) { body.linearVelocity = glm::vec3(0); body.angularVelocity = glm::vec3(0); }
    changed |= EditorUi::DragFloat3("Collision offset (m)", &body.offset.x, 0.01f, -10000.0f, 10000.0f);
    if (body.shape == CollisionShape::Sphere)
        changed |= EditorUi::DragFloat("Radius (m)", &body.radius, 0.01f, 0.001f, 10000.0f);
    if (body.shape == CollisionShape::Box)
        changed |= EditorUi::DragFloat3("Half extents (m)", &body.halfExtents.x, 0.01f, 0.001f, 10000.0f);
    const bool frictionChanged = EditorUi::propertyRow("Friction", [&](const char* id) {
        const bool valueChanged = ImGui::SliderFloat(id, &body.friction, 0.0f, 1.0f);
        if (const char* test = std::getenv("IRIS_PHYSICS_UI_TEST"))
            if (std::strcmp(test, "object") == 0) interactPhysicsControl(valueChanged);
        return valueChanged;
    });
    changed |= frictionChanged;
    changed |= EditorUi::SliderFloat("Restitution", &body.restitution, 0.0f, 1.0f);
    if (body.type == RigidBodyType::Dynamic) {
        changed |= EditorUi::DragFloat("Mass (kg)", &body.mass, 0.05f, 0.001f, 1.0e6f);
        changed |= EditorUi::DragFloat("Linear damping", &body.linearDamping, 0.01f, 0.0f, 100.0f);
        changed |= EditorUi::DragFloat("Angular damping", &body.angularDamping, 0.01f, 0.0f, 100.0f);
        changed |= EditorUi::DragFloat3("Velocity (m/s)", &body.linearVelocity.x, 0.05f, -1000.0f, 1000.0f);
        changed |= EditorUi::DragFloat3("Angular velocity (rad/s)", &body.angularVelocity.x, 0.05f, -1000.0f, 1000.0f);
    }
    ImGui::TextWrapped("%s", EditorUi::chinese ? "碰撞尺寸独立于模型；刚体只支持根实体与单位尺度。" : "Collision dimensions are independent of the model. Rigid bodies require root entities and unit scale.");
    if (changed) {
        EditorCommand command{EditorCommandType::SetEntityRigidBody, entity.id};
        command.rigidBody = body;
        editorSession_.request(std::move(command));
    }
}
