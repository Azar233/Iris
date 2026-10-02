#include "app/Application.h"
#include "optics/CloudNoiseVolume.h"
#include "optics/CloudLightingLut.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include "render/Shader.h"

#include <array>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>
#include "app/EditorDomain.h"
#include "module/BuiltinModules.h"
#include "app/EditorUi.h"

#include "app/FileDialog.h"
#include "render/GpuModel.h"
#include "render/Renderer.h"
#include "scene/SceneDocument.h"

namespace {

bool hasUndersizedDockLeaf(const ImGuiDockNode* node) {
    if (node == nullptr) return true;
    if (node->IsSplitNode()) {
        return hasUndersizedDockLeaf(node->ChildNodes[0])
            || hasUndersizedDockLeaf(node->ChildNodes[1]);
    }
    if (node->Windows.empty()) return false;
    return node->Size.x + 1.0f < EditorUi::minimumDockedPanelSize.x
        || node->Size.y + 1.0f < EditorUi::minimumDockedPanelSize.y;
}

const char* backendName(EditorRenderBackend backend) {
    switch (backend) {
        case EditorRenderBackend::Raster: return "Raster";
        case EditorRenderBackend::CpuPathTraced: return "CPU Path Traced";
        case EditorRenderBackend::GpuPathTraced: return "GPU Path Traced";
    }
    return "Unknown";
}

std::string formatAssetSize(std::uintmax_t bytes) {
    static constexpr std::array<const char*, 4> units{"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1024.0 && unit + 1U < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    char text[64]{};
    std::snprintf(text, sizeof(text), unit == 0U ? "%.0f %s" : "%.1f %s",
                  value, units[unit]);
    return text;
}

} // namespace

void Application::drawWorkspaceToolbar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (!ImGui::BeginViewportSideBar("##WorkspaceToolbar", viewport, ImGuiDir_Up, 38.0f, flags)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginMenuBar()) {
        ImGui::TextDisabled("Backend");
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##WorkspaceBackend", backendName(editorSession_.backend()))) {
            for (int index = 0; index < 3; ++index) {
                const auto backend = static_cast<EditorRenderBackend>(index);
                const bool available = backend != EditorRenderBackend::GpuPathTraced;
                ImGui::BeginDisabled(!available);
                if (ImGui::Selectable(backendName(backend), editorSession_.backend() == backend)) {
                    editorSession_.requestBackend(backend);
                }
                ImGui::EndDisabled();
                if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("GPU Path Tracing is scheduled after the Vulkan raster baseline.");
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        const char* activityLabels[] = {"Edit", "Preview", "Bake", "Render"};
        for (int index = 0; index < 4; ++index) {
            if (index > 0) ImGui::SameLine();
            const auto activity = static_cast<EditorActivity>(index);
            bool selected = editorSession_.activity() == activity;
            if (EditorUi::toolbarToggle(activityLabels[index], &selected) && selected) {
                editorSession_.requestActivity(activity);
            }
        }

        ImGui::Separator();
        ImGui::BeginDisabled(editorSession_.activity() == EditorActivity::Edit);
        if (ImGui::SmallButton(editorSession_.paused() ? "Resume" : "Pause")) {
            editorSession_.requestPause(!editorSession_.paused());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!editorSession_.paused());
        if (ImGui::SmallButton("Single Step")) {
            editorSession_.request(EditorCommand{EditorCommandType::Step});
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) editorSession_.request(EditorCommand{EditorCommandType::Reset});
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Render Frame")) {
            editorSession_.request(EditorCommand{EditorCommandType::RenderFrame});
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Render Sequence")) {
            editorSession_.request(EditorCommand{EditorCommandType::RenderSequence});
        }

        const std::string frameLabel = "Frame " + std::to_string(editorSession_.frame())
            + " | " + editorSession_.taskStatus();
        const float right = ImGui::GetWindowWidth() - ImGui::CalcTextSize(frameLabel.c_str()).x - 12.0f;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        ImGui::TextDisabled("%s", frameLabel.c_str());
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

void Application::processEditorCommands() {
    for (const EditorCommand& command : editorSession_.takeCommands()) {
        switch (command.type) {
            case EditorCommandType::BackendChanged: {
                const auto backend = static_cast<EditorRenderBackend>(command.value);
                const int requested = backend == EditorRenderBackend::CpuPathTraced ? 1 : 0;
                if (viewportRenderMode_ == requested) break;
                viewportRenderMode_ = requested;
                if (viewportRenderMode_ == 0) {
                    cpuPreviewTask_.cancel();
                    cpuPreviewTaskId_ = 0U;
                } else {
                    cpuPreviewRestartRequested_ = true;
                }
                break;
            }
            case EditorCommandType::ActivityChanged: {
                const auto activity = static_cast<EditorActivity>(command.value);
                if (activity == EditorActivity::Edit) {
                    cpuPreviewPaused_ = false;
                    cpuPreviewTask_.setPaused(false);
                    animationPlaying_ = false;
                    editorSession_.setTaskStatus("Idle");
                } else if (activity == EditorActivity::Preview) {
                    animationPlaying_ = true;
                    editorSession_.setTaskStatus("Previewing");
                } else {
                    editorSession_.setTaskStatus("Ready");
                }
                break;
            }
            case EditorCommandType::PauseChanged:
                cpuPreviewPaused_ = command.flag;
                cpuPreviewTask_.setPaused(command.flag);
                animationPlaying_ = !command.flag;
                editorSession_.setTaskStatus(command.flag ? "Paused" : "Previewing");
                break;
            case EditorCommandType::Step:
                editorSession_.setFrame(editorSession_.frame() + 1);
                animationTimeFixed_ = true;
                animationTimeSeconds_ = static_cast<float>(editorSession_.timeSeconds());
                cpuPreviewRestartRequested_ = true;
                editorSession_.setTaskStatus("Stepped");
                break;
            case EditorCommandType::Reset:
                editorSession_.setFrame(editorSession_.startFrame());
                animationTimeFixed_ = true;
                animationTimeSeconds_ = static_cast<float>(editorSession_.timeSeconds());
                cpuPreviewRestartRequested_ = true;
                editorSession_.setTaskStatus("Reset");
                break;
            case EditorCommandType::RenderFrame:
                if (viewportRenderMode_ == 0) pendingScreenshotPath_ = nextScreenshotPath();
                else exportCpuPreview();
                editorSession_.setTaskStatus("Frame requested");
                break;
            case EditorCommandType::RenderSequence:
                submitRenderJob(std::filesystem::u8path(renderJobPathBuffer_.data()));
                assetsPanelOpen_ = true;
                break;
            case EditorCommandType::SubmitRenderJob:
                submitRenderJob(std::filesystem::u8path(command.text));
                break;
            case EditorCommandType::CancelRenderJob:
                cancelRenderJob();
                break;
            case EditorCommandType::MoveRenderJobUp:
            case EditorCommandType::MoveRenderJobDown: {
                std::string error;
                const int direction = command.type == EditorCommandType::MoveRenderJobUp ? -1 : 1;
                if (!renderQueue_->movePending(command.entity, direction, error)) {
                    renderQueueMessage_ = error;
                } else {
                    renderQueueMessage_ = "Pending Render Job reordered.";
                }
                statusMessage_ = renderQueueMessage_;
                break;
            }
            case EditorCommandType::RemoveRenderJob: {
                std::string error;
                renderQueueMessage_ = renderQueue_->remove(command.entity, error)
                    ? "Render Job removed from the queue."
                    : error;
                statusMessage_ = renderQueueMessage_;
                break;
            }
            case EditorCommandType::RetryRenderJob: {
                std::string error;
                renderQueueMessage_ = renderQueue_->retry(command.entity, error)
                    ? "Render Job returned to Pending."
                    : error;
                statusMessage_ = renderQueueMessage_;
                break;
            }
            case EditorCommandType::RefreshAssetCatalog: {
                const std::uint64_t previousGeneration = workspaceAssets_.generation();
                discoverModels();
                if (workspaceAssets_.generation() != previousGeneration) {
                    statusMessage_ = "Workspace asset catalog refreshed: "
                        + std::to_string(workspaceAssets_.records().size()) + " indexed asset(s).";
                }
                break;
            }
            case EditorCommandType::OpenSceneAsset:
                openScene(std::filesystem::u8path(command.text));
                break;
            case EditorCommandType::ImportModelAsset:
                loadModel(std::filesystem::u8path(command.text), true);
                break;
            case EditorCommandType::SelectRenderJobAsset: {
                const std::string path = std::filesystem::u8path(command.text).string();
                std::snprintf(renderJobPathBuffer_.data(), renderJobPathBuffer_.size(),
                              "%s", path.c_str());
                focusRenderQueueTab_ = true;
                assetsPanelOpen_ = true;
                statusMessage_ = "Render Job selected for the Queue: " + path;
                break;
            }
            case EditorCommandType::SetEntityTransform: {
                SceneEntity* entity = scene_.find(static_cast<SceneEntityId>(command.entity));
                const auto finite = [](const EditorVector3Payload& value) {
                    return std::isfinite(value.x) && std::isfinite(value.y)
                        && std::isfinite(value.z);
                };
                const bool scaleValid = command.transform.scale.x >= 0.01f
                    && command.transform.scale.y >= 0.01f
                    && command.transform.scale.z >= 0.01f
                    && command.transform.scale.x <= 100.0f
                    && command.transform.scale.y <= 100.0f
                    && command.transform.scale.z <= 100.0f;
                if (entity == nullptr || !finite(command.transform.translation)
                    || !finite(command.transform.rotationDegrees)
                    || !finite(command.transform.scale) || !scaleValid) {
                    statusMessage_ = "Inspector rejected an invalid entity transform.";
                    break;
                }
                entity->transform.translation = glm::vec3(
                    command.transform.translation.x,
                    command.transform.translation.y,
                    command.transform.translation.z
                );
                entity->transform.rotationDegrees = glm::vec3(
                    command.transform.rotationDegrees.x,
                    command.transform.rotationDegrees.y,
                    command.transform.rotationDegrees.z
                );
                entity->transform.scale = glm::vec3(
                    command.transform.scale.x,
                    command.transform.scale.y,
                    command.transform.scale.z
                );
                editedEntities_.insert(entity->id);
                entity->motionHistoryValid = false;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetEntityTint: {
                SceneEntity* entity = scene_.find(static_cast<SceneEntityId>(command.entity));
                if (entity == nullptr || !std::isfinite(command.color.x)
                    || !std::isfinite(command.color.y) || !std::isfinite(command.color.z)) {
                    statusMessage_ = "Inspector rejected an invalid entity tint.";
                    break;
                }
                entity->tint = glm::vec3(
                    std::clamp(command.color.x, 0.0f, 1.0f),
                    std::clamp(command.color.y, 0.0f, 1.0f),
                    std::clamp(command.color.z, 0.0f, 1.0f)
                );
                editedEntities_.insert(entity->id);
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetEntityCastsShadow:
                if (SceneEntity* entity = scene_.find(static_cast<SceneEntityId>(command.entity))) {
                    entity->castsShadow = command.flag;
                    editedEntities_.insert(entity->id);
                    cpuPreviewRestartRequested_ = true;
                    if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                }
                break;
            case EditorCommandType::SetStageSettings: {
                const auto& stage = command.stage;
                const bool valid = std::isfinite(stage.groundColor.x)
                    && std::isfinite(stage.groundColor.y)
                    && std::isfinite(stage.groundColor.z)
                    && std::isfinite(stage.groundOffset)
                    && stage.groundOffset >= -3.0f && stage.groundOffset <= 0.0f;
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid Stage settings.";
                    break;
                }
                showGroundPlane_ = stage.groundReceiver;
                groundColor_ = glm::vec3(
                    std::clamp(stage.groundColor.x, 0.0f, 1.0f),
                    std::clamp(stage.groundColor.y, 0.0f, 1.0f),
                    std::clamp(stage.groundColor.z, 0.0f, 1.0f)
                );
                groundOffset_ = stage.groundOffset;
                showComparisonObject_ = stage.comparisonObject;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetMaterialSettings: {
                const auto& material = command.material;
                const bool valid = std::isfinite(material.baseColor.x)
                    && std::isfinite(material.baseColor.y)
                    && std::isfinite(material.baseColor.z)
                    && std::isfinite(material.shininess)
                    && material.shininess >= 1.0f && material.shininess <= 256.0f;
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid Material settings.";
                    break;
                }
                rendererSettings_.baseColor = glm::vec3(
                    std::clamp(material.baseColor.x, 0.0f, 1.0f),
                    std::clamp(material.baseColor.y, 0.0f, 1.0f),
                    std::clamp(material.baseColor.z, 0.0f, 1.0f)
                );
                rendererSettings_.shininess = material.shininess;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetDirectionalLightSettings: {
                const auto& lighting = command.directionalLight;
                const float directionLengthSquared = lighting.direction.x * lighting.direction.x
                    + lighting.direction.y * lighting.direction.y
                    + lighting.direction.z * lighting.direction.z;
                const bool valid = std::isfinite(directionLengthSquared)
                    && directionLengthSquared > 1.0e-6f
                    && std::isfinite(lighting.ambientStrength)
                    && std::isfinite(lighting.diffuseStrength)
                    && std::isfinite(lighting.specularStrength)
                    && lighting.ambientStrength >= 0.0f && lighting.ambientStrength <= 1.0f
                    && lighting.diffuseStrength >= 0.0f && lighting.diffuseStrength <= 2.0f
                    && lighting.specularStrength >= 0.0f && lighting.specularStrength <= 2.0f;
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid Directional Light settings.";
                    break;
                }
                rendererSettings_.lightDirection = glm::vec3(
                    std::clamp(lighting.direction.x, -1.0f, 1.0f),
                    std::clamp(lighting.direction.y, -1.0f, 1.0f),
                    std::clamp(lighting.direction.z, -1.0f, 1.0f)
                );
                rendererSettings_.ambientStrength = lighting.ambientStrength;
                rendererSettings_.diffuseStrength = lighting.diffuseStrength;
                rendererSettings_.specularStrength = lighting.specularStrength;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetPbrEnvironmentSettings: {
                const auto& environment = command.pbrEnvironment;
                if (!std::isfinite(environment.environmentIntensity)
                    || environment.environmentIntensity < 0.0f
                    || environment.environmentIntensity > 2.0f
                    || environment.shadowCascadeCount < 1
                    || environment.shadowCascadeCount > 4
                    || !std::isfinite(environment.shadowCascadeSplitLambda)
                    || environment.shadowCascadeSplitLambda < 0.0f
                    || environment.shadowCascadeSplitLambda > 1.0f) {
                    statusMessage_ = "Inspector rejected invalid PBR environment settings.";
                    break;
                }
                rendererSettings_.pbrEnabled = environment.pbrEnabled;
                rendererSettings_.iblEnabled = environment.iblEnabled;
                rendererSettings_.skyboxEnabled = environment.skyboxEnabled;
                rendererSettings_.shadowsEnabled = environment.shadowsEnabled;
                rendererSettings_.shadowCascadeCount = environment.shadowCascadeCount;
                rendererSettings_.shadowCascadeSplitLambda =
                    environment.shadowCascadeSplitLambda;
                rendererSettings_.shadowCascadeDebugView = environment.shadowCascadeDebugView;
                rendererSettings_.coloredTransmissionShadowsEnabled =
                    environment.coloredTransmissionShadowsEnabled;
                rendererSettings_.environmentIntensity = environment.environmentIntensity;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetWaterSettings: {
                const auto& water = command.water;
                const auto within = [](float value, float low, float high) {
                    return std::isfinite(value) && value >= low && value <= high;
                };
                if (water.preset < 0 || water.preset > 3
                    || water.quality < 0 || water.quality > 1
                    || !within(water.level, -10.0f, 10.0f)
                    || !within(water.extent, 20.0f, 2000.0f)
                    || !within(water.amplitude, 0.0f, 2.0f)
                    || !within(water.speed, 0.0f, 5.0f)
                    || !within(water.steepness, 0.0f, 0.9f)
                    || !within(water.foamStrength, 0.0f, 1.0f)
                    || !within(water.roughness, 0.02f, 0.8f)
                    || !within(water.reflectionStrength, 0.0f, 2.0f)
                    || !within(water.rippleStrength, 0.0f, 1.0f)
                    || !within(water.sunGlintStrength, 0.0f, 2.0f)
                    || !within(water.windX, -1.0f, 1.0f)
                    || !within(water.windZ, -1.0f, 1.0f)) {
                    statusMessage_ = "Inspector rejected invalid water settings.";
                    break;
                }
                rendererSettings_.water.enabled = water.enabled;
                rendererSettings_.water.preset = static_cast<WaterPreset>(water.preset);
                rendererSettings_.water.quality = static_cast<WaterQuality>(water.quality);
                rendererSettings_.water.level = water.level;
                rendererSettings_.water.extent = water.extent;
                rendererSettings_.water.amplitude = water.amplitude;
                rendererSettings_.water.speed = water.speed;
                rendererSettings_.water.steepness = water.steepness;
                rendererSettings_.water.foamStrength = water.foamStrength;
                rendererSettings_.water.roughness = water.roughness;
                rendererSettings_.water.reflectionStrength = water.reflectionStrength;
                rendererSettings_.water.rippleStrength = water.rippleStrength;
                rendererSettings_.water.sunGlintStrength = water.sunGlintStrength;
                rendererSettings_.water.windDirection = glm::vec2(water.windX, water.windZ);
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetShadingSettings: {
                const auto& shading = command.shading;
                const auto finiteColor = [](const EditorVector3Payload& value) {
                    return std::isfinite(value.x) && std::isfinite(value.y)
                        && std::isfinite(value.z);
                };
                // NaN fails every range comparison below, so explicit isfinite
                // checks are only needed for the clamped color payloads.
                const bool enumsValid = shading.shadingMode >= 0 && shading.shadingMode <= 1
                    && shading.renderPath >= 0 && shading.renderPath <= 1
                    && shading.gBufferDebugView >= 0 && shading.gBufferDebugView <= 5
                    && shading.stylizedColorGradingLut >= 0 && shading.stylizedColorGradingLut <= 2
                    && shading.stylizedDebugView >= 0 && shading.stylizedDebugView <= 6;
                const bool rangesValid = shading.stylizedBandCount >= 2
                    && shading.stylizedBandCount <= 8
                    && shading.stylizedBandSoftness >= 0.0f
                    && shading.stylizedBandSoftness <= 0.25f
                    && shading.stylizedSpecularSize >= 0.02f
                    && shading.stylizedSpecularSize <= 0.8f
                    && shading.stylizedSpecularSoftness >= 0.0f
                    && shading.stylizedSpecularSoftness <= 0.2f
                    && shading.stylizedRimWidth >= 0.02f && shading.stylizedRimWidth <= 0.9f
                    && shading.stylizedRimSoftness >= 0.0f && shading.stylizedRimSoftness <= 0.3f
                    && shading.stylizedRimIntensity >= 0.0f && shading.stylizedRimIntensity <= 3.0f
                    && shading.stylizedOutlineWidth >= 0.5f
                    && shading.stylizedOutlineWidth <= 6.0f
                    && shading.stylizedOutlineDepthThreshold >= 0.001f
                    && shading.stylizedOutlineDepthThreshold <= 0.12f
                    && shading.stylizedOutlineNormalThreshold >= 0.02f
                    && shading.stylizedOutlineNormalThreshold <= 0.8f
                    && shading.stylizedDitherStrength >= 0.0f
                    && shading.stylizedDitherStrength <= 1.0f
                    && shading.stylizedHeightFogDensity >= 0.0f
                    && shading.stylizedHeightFogDensity <= 2.0f
                    && shading.stylizedHeightFogBaseHeight >= -10.0f
                    && shading.stylizedHeightFogBaseHeight <= 10.0f
                    && shading.stylizedHeightFogFalloff >= 0.01f
                    && shading.stylizedHeightFogFalloff <= 4.0f
                    && shading.stylizedColorGradingStrength >= 0.0f
                    && shading.stylizedColorGradingStrength <= 1.0f;
                const bool colorsValid = finiteColor(shading.stylizedShadowTint)
                    && finiteColor(shading.stylizedRimColor)
                    && finiteColor(shading.stylizedOutlineColor)
                    && finiteColor(shading.stylizedHeightFogColor);
                if (!enumsValid || !rangesValid || !colorsValid) {
                    statusMessage_ = "Inspector rejected invalid shading settings.";
                    break;
                }
                rendererSettings_.shadingMode = static_cast<ShadingMode>(shading.shadingMode);
                rendererSettings_.renderPath = static_cast<RenderPath>(shading.renderPath);
                rendererSettings_.gBufferDebugView =
                    static_cast<GBufferDebugView>(shading.gBufferDebugView);
                rendererSettings_.stylizedBandCount = shading.stylizedBandCount;
                rendererSettings_.stylizedBandSoftness = shading.stylizedBandSoftness;
                rendererSettings_.stylizedSpecularSize = shading.stylizedSpecularSize;
                rendererSettings_.stylizedSpecularSoftness = shading.stylizedSpecularSoftness;
                rendererSettings_.stylizedRimWidth = shading.stylizedRimWidth;
                rendererSettings_.stylizedRimSoftness = shading.stylizedRimSoftness;
                rendererSettings_.stylizedRimIntensity = shading.stylizedRimIntensity;
                rendererSettings_.stylizedShadowTint = glm::vec3(
                    std::clamp(shading.stylizedShadowTint.x, 0.0f, 1.0f),
                    std::clamp(shading.stylizedShadowTint.y, 0.0f, 1.0f),
                    std::clamp(shading.stylizedShadowTint.z, 0.0f, 1.0f)
                );
                rendererSettings_.stylizedRimColor = glm::vec3(
                    std::clamp(shading.stylizedRimColor.x, 0.0f, 1.0f),
                    std::clamp(shading.stylizedRimColor.y, 0.0f, 1.0f),
                    std::clamp(shading.stylizedRimColor.z, 0.0f, 1.0f)
                );
                rendererSettings_.stylizedOutlineEnabled = shading.stylizedOutlineEnabled;
                rendererSettings_.stylizedOutlineWidth = shading.stylizedOutlineWidth;
                rendererSettings_.stylizedOutlineDepthThreshold =
                    shading.stylizedOutlineDepthThreshold;
                rendererSettings_.stylizedOutlineNormalThreshold =
                    shading.stylizedOutlineNormalThreshold;
                rendererSettings_.stylizedOutlineColor = glm::vec3(
                    std::clamp(shading.stylizedOutlineColor.x, 0.0f, 1.0f),
                    std::clamp(shading.stylizedOutlineColor.y, 0.0f, 1.0f),
                    std::clamp(shading.stylizedOutlineColor.z, 0.0f, 1.0f)
                );
                rendererSettings_.stylizedDitherEnabled = shading.stylizedDitherEnabled;
                rendererSettings_.stylizedDitherStrength = shading.stylizedDitherStrength;
                rendererSettings_.stylizedHeightFogEnabled = shading.stylizedHeightFogEnabled;
                rendererSettings_.stylizedHeightFogDensity = shading.stylizedHeightFogDensity;
                rendererSettings_.stylizedHeightFogBaseHeight =
                    shading.stylizedHeightFogBaseHeight;
                rendererSettings_.stylizedHeightFogFalloff = shading.stylizedHeightFogFalloff;
                rendererSettings_.stylizedHeightFogColor = glm::vec3(
                    std::clamp(shading.stylizedHeightFogColor.x, 0.0f, 1.0f),
                    std::clamp(shading.stylizedHeightFogColor.y, 0.0f, 1.0f),
                    std::clamp(shading.stylizedHeightFogColor.z, 0.0f, 1.0f)
                );
                rendererSettings_.stylizedColorGradingEnabled =
                    shading.stylizedColorGradingEnabled;
                rendererSettings_.stylizedColorGradingLut =
                    static_cast<StylizedColorGradingLut>(shading.stylizedColorGradingLut);
                rendererSettings_.stylizedColorGradingStrength =
                    shading.stylizedColorGradingStrength;
                rendererSettings_.stylizedDebugView =
                    static_cast<StylizedDebugView>(shading.stylizedDebugView);
                // Raster-only domain: the reference integrator keeps the physical
                // material and light semantics, so only temporal history drops.
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetPostProcessingSettings: {
                const auto& post = command.postProcessing;
                const bool valid = post.temporalDebugView >= 0 && post.temporalDebugView <= 2
                    && post.ssaoRadius >= 0.05f && post.ssaoRadius <= 2.0f
                    && post.ssaoBias >= 0.0f && post.ssaoBias <= 0.15f
                    && post.ssaoStrength >= 0.1f && post.ssaoStrength <= 3.0f
                    && post.temporalHistoryWeight >= 0.0f
                    && post.temporalHistoryWeight <= 0.98f
                    && post.bloomThreshold >= 0.1f && post.bloomThreshold <= 4.0f
                    && post.bloomIntensity >= 0.0f && post.bloomIntensity <= 1.0f
                    && post.exposure >= 0.1f && post.exposure <= 4.0f;
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid post-processing settings.";
                    break;
                }
                // SSAO and TAA change the resolved HDR scene that history
                // reprojection reuses. Exposure, tone mapping and bloom are
                // applied after history resolution and must not reset it.
                const bool affectsHistory = post.ssaoEnabled != rendererSettings_.ssaoEnabled
                    || post.ssaoRadius != rendererSettings_.ssaoRadius
                    || post.ssaoBias != rendererSettings_.ssaoBias
                    || post.ssaoStrength != rendererSettings_.ssaoStrength
                    || post.temporalAaEnabled != rendererSettings_.temporalAaEnabled
                    || post.temporalHistoryWeight != rendererSettings_.temporalHistoryWeight;
                rendererSettings_.ssaoEnabled = post.ssaoEnabled;
                rendererSettings_.ssaoRadius = post.ssaoRadius;
                rendererSettings_.ssaoBias = post.ssaoBias;
                rendererSettings_.ssaoStrength = post.ssaoStrength;
                rendererSettings_.temporalAaEnabled = post.temporalAaEnabled;
                rendererSettings_.temporalHistoryWeight = post.temporalHistoryWeight;
                rendererSettings_.temporalDebugView = post.temporalDebugView;
                rendererSettings_.toneMapping = post.toneMapping;
                rendererSettings_.bloom = post.bloom;
                rendererSettings_.bloomThreshold = post.bloomThreshold;
                rendererSettings_.bloomIntensity = post.bloomIntensity;
                rendererSettings_.exposure = post.exposure;
                if (affectsHistory && renderer_ != nullptr) {
                    renderer_->invalidateTemporalHistory();
                }
                break;
            }
            case EditorCommandType::SetRasterizationSettings: {
                const auto& raster = command.rasterization;
                const bool valid = (raster.msaaSamples == 1 || raster.msaaSamples == 4)
                    && std::isfinite(raster.backgroundColor.x)
                    && std::isfinite(raster.backgroundColor.y)
                    && std::isfinite(raster.backgroundColor.z);
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid rasterization settings.";
                    break;
                }
                rendererSettings_.wireframe = raster.wireframe;
                rendererSettings_.cullBackFaces = raster.cullBackFaces;
                rendererSettings_.normalMapping = raster.normalMapping;
                rendererSettings_.showGrid = raster.showGrid;
                rendererSettings_.showAxes = raster.showAxes;
                rendererSettings_.backgroundColor = glm::vec3(
                    std::clamp(raster.backgroundColor.x, 0.0f, 1.0f),
                    std::clamp(raster.backgroundColor.y, 0.0f, 1.0f),
                    std::clamp(raster.backgroundColor.z, 0.0f, 1.0f)
                );
                rendererSettings_.msaaSamples = raster.msaaSamples;
                // Wireframe, culling, normal mapping, debug overlays, clear color
                // and MSAA all change what is written into the HDR scene.
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetCameraSettings: {
                const auto& settings = command.camera;
                if (!std::isfinite(settings.fieldOfViewDegrees)
                    || settings.fieldOfViewDegrees < 15.0f
                    || settings.fieldOfViewDegrees > 90.0f
                    || !std::isfinite(settings.farPlane)
                    || settings.farPlane < 100.0f
                    || settings.farPlane > 2000.0f) {
                    statusMessage_ = "Inspector rejected invalid Camera settings.";
                    break;
                }
                camera_.setFieldOfView(settings.fieldOfViewDegrees);
                camera_.setFarPlane(settings.farPlane);
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetRuntimeSettings: {
                const auto& settings = command.runtime;
                applyVsync(settings.vsync);
                rendererSettings_.shaderHotReloadEnabled = settings.shaderHotReloadEnabled;
                break;
            }
            case EditorCommandType::SetGlassSettings: {
                const auto& glass = command.glass;
                const bool valid = glass.refractionScale >= 0.0f
                    && glass.refractionScale <= 0.8f
                    && glass.refractionSteps >= 4 && glass.refractionSteps <= 32
                    && glass.volumeThicknessScale >= 0.0f
                    && glass.volumeThicknessScale <= 4.0f
                    && glass.volumeGlassTransmission >= 0.0f
                    && glass.volumeGlassTransmission <= 1.0f
                    && glass.volumeGlassRoughness >= 0.04f
                    && glass.volumeGlassRoughness <= 1.0f
                    && glass.volumeGlassAttenuationDistance >= 0.05f
                    && glass.volumeGlassAttenuationDistance <= 8.0f
                    && glass.dispersionStrength >= 0.0f && glass.dispersionStrength <= 2.5f
                    && glass.glassDebugView >= 0 && glass.glassDebugView <= 12
                    && std::isfinite(glass.volumeGlassAttenuationColor.x)
                    && std::isfinite(glass.volumeGlassAttenuationColor.y)
                    && std::isfinite(glass.volumeGlassAttenuationColor.z);
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid Glass settings.";
                    break;
                }
                rendererSettings_.transmissionEnabled = glass.transmissionEnabled;
                rendererSettings_.dispersionEnabled = glass.dispersionEnabled;
                rendererSettings_.geometricThicknessEnabled = glass.geometricThicknessEnabled;
                rendererSettings_.twoInterfaceRefractionEnabled =
                    glass.twoInterfaceRefractionEnabled;
                rendererSettings_.refractionScale = glass.refractionScale;
                rendererSettings_.refractionSteps = glass.refractionSteps;
                rendererSettings_.volumeThicknessScale = glass.volumeThicknessScale;
                rendererSettings_.volumeGlassOverrideEnabled =
                    glass.volumeGlassOverrideEnabled;
                rendererSettings_.volumeGlassTransmission = glass.volumeGlassTransmission;
                rendererSettings_.volumeGlassRoughness = glass.volumeGlassRoughness;
                rendererSettings_.volumeGlassAttenuationColor = glm::vec3(
                    std::clamp(glass.volumeGlassAttenuationColor.x, 0.0f, 1.0f),
                    std::clamp(glass.volumeGlassAttenuationColor.y, 0.0f, 1.0f),
                    std::clamp(glass.volumeGlassAttenuationColor.z, 0.0f, 1.0f)
                );
                rendererSettings_.volumeGlassAttenuationDistance =
                    glass.volumeGlassAttenuationDistance;
                rendererSettings_.dispersionStrength = glass.dispersionStrength;
                rendererSettings_.glassDebugView =
                    static_cast<GlassDebugView>(glass.glassDebugView);
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetCausticsSettings: {
                const auto& caustics = command.caustics;
                const bool valid = caustics.causticsMode >= 0 && caustics.causticsMode <= 1
                    && caustics.causticsStrength >= 0.0f && caustics.causticsStrength <= 8.0f
                    && caustics.causticsScale >= 0.1f && caustics.causticsScale <= 3.0f
                    && caustics.causticsSharpness >= 0.0f && caustics.causticsSharpness <= 1.0f
                    && std::isfinite(caustics.causticsDirection.x)
                    && std::isfinite(caustics.causticsDirection.y)
                    && std::isfinite(caustics.causticsDirection.z);
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid caustics settings.";
                    break;
                }
                rendererSettings_.causticsEnabled = caustics.causticsEnabled;
                rendererSettings_.causticsMode = static_cast<CausticsMode>(caustics.causticsMode);
                rendererSettings_.causticsStrength = caustics.causticsStrength;
                rendererSettings_.causticsScale = caustics.causticsScale;
                rendererSettings_.causticsDirection = glm::vec3(
                    std::clamp(caustics.causticsDirection.x, -1.5f, 1.5f),
                    std::clamp(caustics.causticsDirection.y, -1.5f, 1.5f),
                    std::clamp(caustics.causticsDirection.z, -1.5f, 1.5f)
                );
                rendererSettings_.causticsSharpness = caustics.causticsSharpness;
                rendererSettings_.causticsAnimated = caustics.causticsAnimated;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetInstanceSettings: {
                const auto& instance = command.instance;
                // Culling and LOD stay dormant while batching is off, exactly like the
                // disabled control group in the Inspector, so the payload is applied
                // as submitted instead of being rejected.
                rendererSettings_.instanceOptimizationEnabled =
                    instance.instanceOptimizationEnabled;
                rendererSettings_.frustumCullingEnabled = instance.frustumCullingEnabled;
                rendererSettings_.lodSelectionEnabled = instance.lodSelectionEnabled;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetAtmosphereSettings: {
                const auto& atmosphere = command.atmosphere;
                // The sun is the one input the sky, the light and the shadows share, so an
                // out-of-range value here would desynchronise all three. The entry point
                // rejects instead of clamping: the Inspector already bounds every control.
                const bool valid = std::isfinite(atmosphere.sunElevationDegrees)
                    && atmosphere.sunElevationDegrees >= -10.0f
                    && atmosphere.sunElevationDegrees <= 90.0f
                    && std::isfinite(atmosphere.sunAzimuthDegrees)
                    && atmosphere.sunAzimuthDegrees >= 0.0f
                    && atmosphere.sunAzimuthDegrees <= 360.0f
                    && std::isfinite(atmosphere.turbidity)
                    && atmosphere.turbidity >= 0.0f && atmosphere.turbidity <= 10.0f
                    && std::isfinite(atmosphere.skyIntensity)
                    && atmosphere.skyIntensity >= 0.0f && atmosphere.skyIntensity <= 20.0f
                    && std::isfinite(atmosphere.sunIntensity)
                    && atmosphere.sunIntensity >= 0.0f && atmosphere.sunIntensity <= 8.0f
                    && std::isfinite(atmosphere.groundAlbedo)
                    && atmosphere.groundAlbedo >= 0.0f && atmosphere.groundAlbedo <= 1.0f
                    && std::isfinite(atmosphere.moonIntensity)
                    && atmosphere.moonIntensity >= 0.0f && atmosphere.moonIntensity <= 4.0f
                    && std::isfinite(atmosphere.starIntensity)
                    && atmosphere.starIntensity >= 0.0f && atmosphere.starIntensity <= 4.0f
                    && std::isfinite(atmosphere.aerialPerspectiveStrength)
                    && atmosphere.aerialPerspectiveStrength >= 0.0f
                    && atmosphere.aerialPerspectiveStrength <= 4.0f
                    && std::isfinite(atmosphere.aerialPerspectiveScaleHeight)
                    && atmosphere.aerialPerspectiveScaleHeight >= 0.01f
                    && atmosphere.aerialPerspectiveScaleHeight <= 20000.0f
                    && std::isfinite(atmosphere.cloudBaseHeight)
                    && atmosphere.cloudBaseHeight >= 0.0f
                    && atmosphere.cloudBaseHeight <= 20000.0f
                    && std::isfinite(atmosphere.cloudTopHeight)
                    && atmosphere.cloudTopHeight >= 0.0f
                    && atmosphere.cloudTopHeight <= 30000.0f
                    && std::isfinite(atmosphere.cloudCoverage)
                    && atmosphere.cloudCoverage >= 0.0f && atmosphere.cloudCoverage <= 1.0f
                    && std::isfinite(atmosphere.cloudDensity)
                    && atmosphere.cloudDensity >= 0.0f && atmosphere.cloudDensity <= 4.0f
                    && std::isfinite(atmosphere.cloudWindOffsetX)
                    && atmosphere.cloudWindOffsetX >= -60000.0f
                    && atmosphere.cloudWindOffsetX <= 60000.0f
                    && std::isfinite(atmosphere.cloudWindOffsetZ)
                    && atmosphere.cloudWindOffsetZ >= -60000.0f
                    && atmosphere.cloudWindOffsetZ <= 60000.0f
                    && std::isfinite(atmosphere.cloudFeatureScale)
                    && atmosphere.cloudFeatureScale >= 10.0f
                    && atmosphere.cloudFeatureScale <= 200000.0f
                    && std::isfinite(atmosphere.cloudNoisePeriod)
                    && atmosphere.cloudNoisePeriod >= 1.0f && atmosphere.cloudNoisePeriod <= 16.0f
                    && (!atmosphere.cloudOfflineNoise || std::lround(atmosphere.cloudNoisePeriod)==4)
                    && std::isfinite(atmosphere.cloudWeatherScale)
                    && atmosphere.cloudWeatherScale >= 100.0f
                    && atmosphere.cloudWeatherScale <= 400000.0f
                    && std::isfinite(atmosphere.cloudCoverageVariation)
                    && atmosphere.cloudCoverageVariation >= 0.0f
                    && atmosphere.cloudCoverageVariation <= 1.0f
                    && std::isfinite(atmosphere.cloudType)
                    && atmosphere.cloudType >= 0.0f && atmosphere.cloudType <= 1.0f
                    && std::isfinite(atmosphere.cloudTypeVariation)
                    && atmosphere.cloudTypeVariation >= 0.0f
                    && atmosphere.cloudTypeVariation <= 1.0f
                    && std::isfinite(atmosphere.cloudHeightVariation)
                    && atmosphere.cloudHeightVariation >= 0.0f
                    && atmosphere.cloudHeightVariation <= 1.0f
                    && std::isfinite(atmosphere.cloudDetailStrength)
                    && atmosphere.cloudDetailStrength >= 0.0f
                    && atmosphere.cloudDetailStrength <= 1.0f
                    && std::isfinite(atmosphere.cloudDetailEdge)
                    && atmosphere.cloudDetailEdge >= 0.0f
                    && atmosphere.cloudDetailEdge <= 1.0f
                    && atmosphere.cloudQuality >= 0 && atmosphere.cloudQuality <= 1
                    && std::isfinite(atmosphere.cloudHorizonFadeDegrees)
                    && atmosphere.cloudHorizonFadeDegrees >= 0.0f
                    && atmosphere.cloudHorizonFadeDegrees <= 30.0f
                    && std::isfinite(atmosphere.cloudAmbientElevationDegrees)
                    && atmosphere.cloudAmbientElevationDegrees >= 0.0f
                    && atmosphere.cloudAmbientElevationDegrees <= 89.0f
                    && std::isfinite(atmosphere.cloudAmbientScale)
                    && atmosphere.cloudAmbientScale >= 0.0f
                    && atmosphere.cloudAmbientScale <= 4.0f;
                if (!valid) {
                    statusMessage_ = "Inspector rejected invalid atmosphere settings.";
                    break;
                }
                if(atmosphere.cloudOfflineNoise) {
                    try {
                        (void)cloud::canonicalNoiseVolume();
                        (void)cloud::canonicalLightingLut();
                    }
                    catch(const std::exception& error) { statusMessage_=error.what();break; }
                }
                rendererSettings_.atmosphere.enabled = atmosphere.enabled;
                rendererSettings_.atmosphere.sunElevationDegrees =
                    atmosphere.sunElevationDegrees;
                rendererSettings_.atmosphere.sunAzimuthDegrees =
                    atmosphere.sunAzimuthDegrees;
                rendererSettings_.atmosphere.turbidity = atmosphere.turbidity;
                rendererSettings_.atmosphere.skyIntensity = atmosphere.skyIntensity;
                rendererSettings_.atmosphere.sunIntensity = atmosphere.sunIntensity;
                rendererSettings_.atmosphere.groundAlbedo = atmosphere.groundAlbedo;
                rendererSettings_.atmosphere.nightSkyEnabled = atmosphere.nightSkyEnabled;
                rendererSettings_.atmosphere.moonIntensity = atmosphere.moonIntensity;
                rendererSettings_.atmosphere.starIntensity = atmosphere.starIntensity;
                rendererSettings_.atmosphere.aerialPerspectiveEnabled =
                    atmosphere.aerialPerspectiveEnabled;
                rendererSettings_.atmosphere.aerialPerspectiveStrength =
                    atmosphere.aerialPerspectiveStrength;
                rendererSettings_.atmosphere.aerialPerspectiveScaleHeight =
                    atmosphere.aerialPerspectiveScaleHeight;
                rendererSettings_.atmosphere.cloudsEnabled = atmosphere.cloudsEnabled;
                rendererSettings_.atmosphere.cloudBaseHeight = atmosphere.cloudBaseHeight;
                rendererSettings_.atmosphere.cloudTopHeight = atmosphere.cloudTopHeight;
                rendererSettings_.atmosphere.cloudCoverage = atmosphere.cloudCoverage;
                rendererSettings_.atmosphere.cloudDensity = atmosphere.cloudDensity;
                rendererSettings_.atmosphere.cloudWindOffsetX = atmosphere.cloudWindOffsetX;
                rendererSettings_.atmosphere.cloudWindOffsetZ = atmosphere.cloudWindOffsetZ;
                rendererSettings_.atmosphere.cloudFeatureScale = atmosphere.cloudFeatureScale;
                rendererSettings_.atmosphere.cloudNoisePeriod = atmosphere.cloudNoisePeriod;
                rendererSettings_.atmosphere.cloudWeatherScale = atmosphere.cloudWeatherScale;
                rendererSettings_.atmosphere.cloudCoverageVariation =
                    atmosphere.cloudCoverageVariation;
                rendererSettings_.atmosphere.cloudType = atmosphere.cloudType;
                rendererSettings_.atmosphere.cloudTypeVariation = atmosphere.cloudTypeVariation;
                rendererSettings_.atmosphere.cloudHeightVariation =
                    atmosphere.cloudHeightVariation;
                rendererSettings_.atmosphere.cloudDetailStrength =
                    atmosphere.cloudDetailStrength;
                rendererSettings_.atmosphere.cloudDetailEdge = atmosphere.cloudDetailEdge;
                rendererSettings_.atmosphere.cloudHalfResolution = atmosphere.cloudHalfResolution;
                rendererSettings_.atmosphere.cloudTemporalEnabled = atmosphere.cloudTemporalEnabled;
                rendererSettings_.atmosphere.cloudShadowsEnabled = atmosphere.cloudShadowsEnabled;
                rendererSettings_.atmosphere.cloudGodRaysEnabled = atmosphere.cloudGodRaysEnabled;
                rendererSettings_.atmosphere.cloudDeterministic = atmosphere.cloudDeterministic;
                rendererSettings_.atmosphere.cloudOfflineNoise = atmosphere.cloudOfflineNoise;
                rendererSettings_.atmosphere.cloudGodRaysStrength = std::clamp(atmosphere.cloudGodRaysStrength, 0.0f, 1.0f);
                rendererSettings_.atmosphere.cloudQuality = atmosphere.cloudQuality == 1
                    ? atmosphere::CloudQualityTier::High
                    : atmosphere::CloudQualityTier::Low;
                rendererSettings_.atmosphere.cloudHorizonFadeDegrees =
                    atmosphere.cloudHorizonFadeDegrees;
                rendererSettings_.atmosphere.cloudAmbientElevationDegrees =
                    atmosphere.cloudAmbientElevationDegrees;
                rendererSettings_.atmosphere.cloudAmbientScale = atmosphere.cloudAmbientScale;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::FrameCamera: {
                const auto target = static_cast<EditorCameraFrameTarget>(command.value);
                if (command.value
                    > static_cast<std::uint64_t>(EditorCameraFrameTarget::Model)) {
                    statusMessage_ = "Inspector rejected an unknown camera frame target.";
                    break;
                }
                switch (target) {
                    case EditorCameraFrameTarget::Default:
                        camera_.reset();
                        break;
                    case EditorCameraFrameTarget::Selection: {
                        const SceneEntity* selected = scene_.find(selectedSceneEntity_);
                        camera_.reset(selected != nullptr
                            ? glm::vec3(selected->worldTransform[3])
                            : modelPosition_);
                        break;
                    }
                    case EditorCameraFrameTarget::Model:
                        camera_.reset(modelPosition_);
                        break;
                }
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetActiveModule: {
                if (command.text == activeModuleId_) break;
                activeModuleId_ = command.text;
                moduleParameterOverrides_.clear();
                moduleRuntime_.clear();
                ++moduleInputRevision_;
                if (activeModuleId_.empty()) {
                    moduleMessage_ = "No module is active.";
                    statusMessage_ = "Module preview disabled.";
                } else if (moduleRegistry_.contains(activeModuleId_)) {
                    moduleMessage_ = "Module '" + activeModuleId_ + "' selected.";
                    statusMessage_ = moduleMessage_;
                } else {
                    moduleMessage_ = "Unknown module id: " + activeModuleId_;
                    statusMessage_ = moduleMessage_;
                }
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetModuleParameter: {
                const auto& payload = command.moduleParameter;
                if (payload.type < 0
                    || payload.type > static_cast<int>(ModuleParameterType::Asset)) {
                    statusMessage_ = "Inspector rejected an unknown module parameter type.";
                    break;
                }
                ModuleParameterValue value;
                value.type = static_cast<ModuleParameterType>(payload.type);
                value.boolean = payload.boolean;
                value.integer = payload.integer;
                value.number = payload.number;
                value.color = glm::vec3(
                    command.color.x, command.color.y, command.color.z
                );
                value.text = payload.text;
                // Replace the existing override for this parameter, or append it. Only
                // overrides are stored, so a parameter left at its default stays out of
                // the persisted set.
                bool replaced = false;
                for (ModuleParameterOverride& entry : moduleParameterOverrides_) {
                    if (entry.id != command.text) continue;
                    entry.value = value;
                    replaced = true;
                    break;
                }
                if (!replaced) {
                    moduleParameterOverrides_.push_back(
                        ModuleParameterOverride{command.text, value}
                    );
                }
                ++moduleInputRevision_;
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::SetModuleSeed: {
                const std::uint32_t seed = command.value > 0xFFFFFFFFULL
                    ? 0U
                    : static_cast<std::uint32_t>(command.value);
                if (seed == moduleSeed_) break;
                moduleSeed_ = seed;
                ++moduleInputRevision_;
                statusMessage_ = "Module seed set to " + std::to_string(moduleSeed_) + ".";
                cpuPreviewRestartRequested_ = true;
                if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                break;
            }
            case EditorCommandType::DuplicateEntity:
                selectEntity(scene_.duplicateEntity(static_cast<SceneEntityId>(command.entity)));
                break;
            case EditorCommandType::DeleteEntity:
                if (selectedSceneEntity_ == static_cast<SceneEntityId>(command.entity)) deleteSelectedEntity();
                break;
            case EditorCommandType::SetEntityVisibility:
                if (SceneEntity* entity = scene_.find(static_cast<SceneEntityId>(command.entity))) {
                    entity->visible = command.flag;
                    editedEntities_.insert(entity->id);
                    cpuPreviewRestartRequested_ = true;
                    if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                }
                break;
            case EditorCommandType::SetEntityParent:
            {
                const auto childId = static_cast<SceneEntityId>(command.entity);
                const auto parentId = static_cast<SceneEntityId>(command.value);
                const SceneEntity* child = scene_.find(childId);
                const SceneEntityId previousParent = child == nullptr
                    ? invalidSceneEntityId : child->parent;
                if (child == nullptr || !scene_.setParent(childId, parentId)) {
                    statusMessage_ = "Scene rejected an invalid parent relationship.";
                    break;
                }
                if (previousParent != parentId) {
                    // A parent change affects the whole subtree, including temporal data.
                    for (const SceneEntity& candidate : scene_.entities()) {
                        SceneEntityId ancestor = candidate.id;
                        for (std::size_t depth = 0; depth < scene_.size(); ++depth) {
                            if (ancestor == childId) {
                                scene_.find(candidate.id)->motionHistoryValid = false;
                                break;
                            }
                            const SceneEntity* current = scene_.find(ancestor);
                            if (current == nullptr || current->parent == invalidSceneEntityId) break;
                            ancestor = current->parent;
                        }
                    }
                    editedEntities_.insert(childId);
                    cpuPreviewRestartRequested_ = true;
                    if (renderer_ != nullptr) renderer_->invalidateTemporalHistory();
                }
                break;
            }
        }
    }
}

void Application::submitRenderJob(const std::filesystem::path& path) {
    std::filesystem::path resolvedPath = path;
    if (resolvedPath.is_relative()) resolvedPath = sourceRoot_ / resolvedPath;
    std::uint64_t id = 0U;
    std::string error;
    if (!renderQueue_->enqueue(resolvedPath, id, error)) {
        renderQueueMessage_ = "Render Job rejected: " + error;
        statusMessage_ = renderQueueMessage_;
        editorSession_.setTaskStatus("Failed");
        return;
    }
    renderQueueMessage_ = "Render Job #" + std::to_string(id) + " added to Pending.";
    statusMessage_ = renderQueueMessage_;
    editorSession_.setTaskStatus("Pending");
    editorSession_.requestActivity(EditorActivity::Render);
}

void Application::updateRenderQueue() {
    renderQueue_->update();
    const auto queueEntries = renderQueue_->entries();
    if (queueEntries.empty()) {
        editorSession_.setTaskStatus("Idle");
        return;
    }
    const auto active = std::find_if(queueEntries.begin(), queueEntries.end(), [](const auto& entry) {
        return entry.status == RenderQueueStatus::Running
            || entry.status == RenderQueueStatus::Cancelling;
    });
    if (active != queueEntries.end()) {
        editorSession_.setTaskStatus(renderQueueStatusName(active->status));
        return;
    }
    const auto pending = std::find_if(queueEntries.begin(), queueEntries.end(), [](const auto& entry) {
        return entry.status == RenderQueueStatus::Pending;
    });
    editorSession_.setTaskStatus(pending != queueEntries.end()
        ? "Pending"
        : renderQueueStatusName(queueEntries.back().status));
}

void Application::cancelRenderJob() {
    std::string error;
    if (!renderQueue_->cancelActive(error)) {
        renderQueueMessage_ = error;
        statusMessage_ = renderQueueMessage_;
        return;
    }
    renderQueueMessage_ = "Cancellation requested for the active Render Job.";
    statusMessage_ = renderQueueMessage_;
    editorSession_.setTaskStatus("Cancelling");
}

void Application::drawScenePanel() {
    ImGui::SetNextWindowSizeConstraints(
        EditorUi::minimumDockedPanelSize,
        ImVec2(FLT_MAX, FLT_MAX)
    );
    if (!ImGui::Begin(EditorUi::label("Scene Explorer###Hierarchy"))) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText(EditorUi::label("Scene objects"));
    if (!scene_.entities().empty()) {
        const char* rootLabel = EditorUi::chinese
            ? "场景根节点（拖放对象到此）" : "Scene root (drop an object here)";
        ImGui::Selectable(rootLabel, false, ImGuiSelectableFlags_SpanAvailWidth);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MYRENDERER_SCENE_ENTITY")) {
                if (payload->DataSize == sizeof(SceneEntityId)) {
                    const auto id = *static_cast<const SceneEntityId*>(payload->Data);
                    editorSession_.request(EditorCommand{
                        EditorCommandType::SetEntityParent, id, invalidSceneEntityId
                    });
                }
            }
            ImGui::EndDragDropTarget();
        }

        std::unordered_map<SceneEntityId, std::vector<const SceneEntity*>> children;
        std::vector<const SceneEntity*> roots;
        for (const SceneEntity& entity : scene_.entities()) {
            if (!entity.enabledByPreset) continue;
            const SceneEntity* parent = scene_.find(entity.parent);
            if (parent != nullptr && parent->enabledByPreset) children[entity.parent].push_back(&entity);
            else roots.push_back(&entity);
        }
        const auto drawEntity = [&](const auto& self, const SceneEntity& entity) -> void {
            const std::string stableId = std::to_string(entity.id);
            ImGui::PushID(stableId.c_str());
            bool visible = entity.visible;
            if (EditorUi::Checkbox("##Visibility", &visible)) {
                editorSession_.request(EditorCommand{
                    EditorCommandType::SetEntityVisibility, entity.id, 0U, visible
                });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", EditorUi::chinese
                    ? (visible ? "可见" : "隐藏") : (visible ? "Visible" : "Hidden"));
            }
            ImGui::SameLine(0.0f, 4.0f);
            const auto childIt = children.find(entity.id);
            const bool hasChildren = childIt != children.end() && !childIt->second.empty();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
                | ImGuiTreeNodeFlags_OpenOnArrow
                | (selectedSceneEntity_ == entity.id ? ImGuiTreeNodeFlags_Selected : 0);
            if (hasChildren) flags |= ImGuiTreeNodeFlags_DefaultOpen;
            else flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            const bool open = ImGui::TreeNodeEx("##SceneEntity", flags, "%s", entity.name.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)
                || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                selectEntity(entity.id);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entity.name.c_str());
            if (ImGui::BeginDragDropSource()) {
                const SceneEntityId id = entity.id;
                ImGui::SetDragDropPayload("MYRENDERER_SCENE_ENTITY", &id, sizeof(id));
                ImGui::TextUnformatted(entity.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MYRENDERER_SCENE_ENTITY")) {
                    if (payload->DataSize == sizeof(SceneEntityId)) {
                        const auto id = *static_cast<const SceneEntityId*>(payload->Data);
                        if (id != entity.id) {
                            editorSession_.request(EditorCommand{
                                EditorCommandType::SetEntityParent, id, entity.id
                            });
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            const float nameWidth = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 28.0f;
            if (ImGui::CalcTextSize(entity.name.c_str()).x > nameWidth) {
                ImGui::TextWrapped("%s", entity.name.c_str());
            }
            if (hasChildren && open) {
                for (const SceneEntity* child : childIt->second) self(self, *child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        for (const SceneEntity* root : roots) drawEntity(drawEntity, *root);

        ImGui::Separator();
        ImGui::BeginDisabled(selectedSceneEntity_ == invalidSceneEntityId);
        if (ImGui::Button(EditorUi::label("Duplicate selected"))) {
            editorSession_.request(EditorCommand{
                EditorCommandType::DuplicateEntity,
                static_cast<std::uint64_t>(selectedSceneEntity_)
            });
        }
        ImGui::SameLine();
        if (ImGui::Button(EditorUi::label("Delete"))) {
            editorSession_.request(EditorCommand{
                EditorCommandType::DeleteEntity,
                static_cast<std::uint64_t>(selectedSceneEntity_)
            });
        }
        ImGui::EndDisabled();
        if (SceneEntity* selected = scene_.find(selectedSceneEntity_)) {
            const char* parentName = "None";
            if (const SceneEntity* parent = scene_.find(selected->parent)) parentName = parent->name.c_str();
            if (ImGui::BeginCombo(EditorUi::label("Parent"), parentName)) {
                if (ImGui::Selectable(EditorUi::label("None"), selected->parent == invalidSceneEntityId)) {
                    editorSession_.request(EditorCommand{
                        EditorCommandType::SetEntityParent,
                        static_cast<std::uint64_t>(selected->id),
                        static_cast<std::uint64_t>(invalidSceneEntityId)
                    });
                }
                for (const SceneEntity& candidate : scene_.entities()) {
                    if (candidate.id == selected->id) continue;
                    ImGui::PushID(&candidate);
                    const bool isParent = candidate.id == selected->parent;
                    if (ImGui::Selectable(candidate.name.c_str(), isParent)) {
                        editorSession_.request(EditorCommand{
                            EditorCommandType::SetEntityParent,
                            static_cast<std::uint64_t>(selected->id),
                            static_cast<std::uint64_t>(candidate.id)
                        });
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        if (EditorUi::section("Scene statistics")) {
            ImGui::TextDisabled("Entities: %zu", scene_.size());
            ImGui::TextDisabled("Meshes: %zu", loadedMeshCount_);
            ImGui::TextDisabled("Submeshes: %zu", loadedSubmeshCount_);
            ImGui::TextDisabled("Vertices: %zu", loadedVertexCount_);
            ImGui::TextDisabled("Triangles: %zu", loadedTriangleCount_);
            if (lightStressDemoEnabled_) {
                ImGui::TextDisabled("Local lights: %zu", rendererSettings_.localLights.size());
            }
            if (instanceStressDemoEnabled_) {
                ImGui::TextDisabled("Visible / culled: %zu / %zu",
                    renderer_->visibleInstanceCount(), renderer_->culledInstanceCount());
            }
        }
    } else {
        ImGui::TextDisabled("%s", EditorUi::chinese ? "场景为空" : "Scene is empty");
    }

    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput
        && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        editorSession_.request(EditorCommand{
            EditorCommandType::DeleteEntity,
            static_cast<std::uint64_t>(selectedSceneEntity_)
        });
    }
    ImGui::End();
}

void Application::drawLogProfilePanel() {
    ImGui::TextWrapped("%s", statusMessage_.c_str());
    if (pendingModelImport_.has_value()) {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - pendingModelImport_->startedAt).count();
        const float activity = static_cast<float>(std::fmod(elapsed * 0.35, 1.0));
        ImGui::ProgressBar(activity, ImVec2(-1.0f, 0.0f), "Importing on CPU...");
    }
    ImGui::Separator();

    if (EditorUi::section("Render tasks", true)) {
        const std::vector<RenderQueueEntrySnapshot> entries = renderQueue_->entries();
        if (entries.empty()) {
            ImGui::TextDisabled("No Render Job has been submitted.");
        }
        for (const RenderQueueEntrySnapshot& entry : entries) {
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::Text(
                "#%llu %s | %s",
                static_cast<unsigned long long>(entry.id),
                renderQueueStatusName(entry.status),
                entry.jobPath.filename().string().c_str()
            );
            ImGui::TextDisabled(
                "frames %d..%d @ %d FPS | complete %d skipped %d failed %d | outputs %zu",
                entry.startFrame,
                entry.endFrame,
                entry.framesPerSecond,
                entry.completedFrames,
                entry.skippedFrames,
                entry.failedFrames,
                entry.outputCount
            );
            if (!entry.message.empty()) {
                ImGui::TextWrapped("%s", entry.message.c_str());
            }
            ImGui::PopID();
        }
    }

    if (EditorUi::section("Runtime profile", true)) {
        ImGui::Text("CPU frame: %.2f ms", cpuFrameTimeMilliseconds_);
        if (renderer_->hasGpuFrameTime()) {
            ImGui::Text("GPU frame: %.3f ms", renderer_->gpuFrameTimeMilliseconds());
        } else {
            ImGui::TextDisabled("GPU frame: collecting...");
        }
        ImGui::Text(
            "Draw calls: %zu | triangles: %zu | active passes: %zu",
            renderer_->drawCallCount(),
            loadedTriangleCount_,
            renderer_->activePassNames().size()
        );
        ImGui::Text(
            "RenderTarget estimate: %.1f MiB | opaque traffic: %.1f MiB/frame",
            static_cast<double>(renderer_->estimatedRenderMemoryBytes()) / (1024.0 * 1024.0),
            static_cast<double>(renderer_->estimatedOpaqueTrafficBytesPerFrame()) / (1024.0 * 1024.0)
        );
        if (lastLoadTotalMilliseconds_ > 0.0) {
            ImGui::TextDisabled(
                "Last load: %.1f ms CPU + %.1f ms GPU = %.1f ms",
                lastCpuImportMilliseconds_,
                lastGpuUploadMilliseconds_,
                lastLoadTotalMilliseconds_
            );
        }
        if (!renderer_->activePassNames().empty() && ImGui::TreeNode("GPU passes")) {
            for (std::size_t passIndex = 0;
                 passIndex < renderer_->activePassNames().size();
                 ++passIndex) {
                const std::string& passName = renderer_->activePassNames()[passIndex];
                const auto timing = std::find_if(
                    renderer_->gpuPassTimings().begin(),
                    renderer_->gpuPassTimings().end(),
                    [&](const GpuPassTiming& candidate) { return candidate.name == passName; }
                );
                if (timing != renderer_->gpuPassTimings().end()) {
                    ImGui::BulletText("%s: %.3f ms", passName.c_str(), timing->milliseconds);
                } else {
                    ImGui::BulletText("%s: collecting...", passName.c_str());
                }
            }
            ImGui::TreePop();
        }
    }

    if (modulePreviewEnabled() && moduleRuntime_.active()
        && EditorUi::section("Module log", true)) {
        const std::vector<ModuleLogEntry>& entries = moduleRuntime_.logEntries();
        if (entries.empty()) {
            ImGui::TextDisabled("No module output for %s.", activeModuleId_.c_str());
        }
        for (const ModuleLogEntry& entry : entries) {
            ImGui::TextDisabled(
                "[%s] frame %d: %s",
                moduleLogSeverityName(entry.severity),
                entry.frame,
                entry.message.c_str()
            );
        }
    }

    drawDiagnostics();
}

void Application::drawModulePanel() {
    ImGui::TextWrapped(
        "A C++ module drives a discardable runtime scene for the current frame; the "
        "unsaved edit scene is never written by a module."
    );
    ImGui::Separator();

    const std::vector<ModuleManifest> manifests = moduleRegistry_.manifests();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(
            "##ActiveModule",
            activeModuleId_.empty() ? "None" : activeModuleId_.c_str()
        )) {
        if (ImGui::Selectable("None", activeModuleId_.empty())) {
            EditorCommand command{EditorCommandType::SetActiveModule};
            editorSession_.request(std::move(command));
        }
        for (const ModuleManifest& manifest : manifests) {
            const bool selected = manifest.id == activeModuleId_;
            if (ImGui::Selectable(manifest.displayName.c_str(), selected)) {
                EditorCommand command{EditorCommandType::SetActiveModule};
                command.text = manifest.id;
                editorSession_.request(std::move(command));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s | %s | %s", manifest.id.c_str(),
                                  manifest.cmakeTarget.c_str(), manifest.sourceRoot.c_str());
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    EditorUi::tooltip("Active module");

    if (const ModuleManifest* manifest = moduleRegistry_.find(activeModuleId_)) {
        ImGui::TextDisabled(
            "%s | API %d | %s",
            moduleKindName(manifest->kind),
            manifest->apiVersion,
            manifest->buildId.c_str()
        );
        ImGui::TextDisabled("%s @ %s", manifest->cmakeTarget.c_str(), manifest->sourceRoot.c_str());
    }

    int seed = static_cast<int>(moduleSeed_);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputInt("##ModuleSeed", &seed)) {
        EditorCommand command{EditorCommandType::SetModuleSeed};
        command.value = static_cast<std::uint64_t>(std::max(seed, 0));
        editorSession_.request(std::move(command));
    }
    EditorUi::tooltip("Module seed");

    ImGui::TextWrapped("%s", moduleMessage_.c_str());
    if (!modulePreviewEnabled()) {
        ImGui::TextDisabled(
            "Select a module to preview it here; the Viewport, the CPU path traced "
            "preview and the frame report then all use the same run."
        );
        return;
    }

    const ModuleRunReport& report = moduleRuntime_.report();
    if (moduleRuntime_.active()) {
        ImGui::TextDisabled(
            "frame %d / %d | input %llu | content %llu",
            report.lastFrame,
            report.endFrame,
            static_cast<unsigned long long>(report.inputContentHash),
            static_cast<unsigned long long>(report.contentHash)
        );

        const auto requestParameter = [&](const ModuleParameterDescriptor& descriptor,
                                          const ModuleParameterValue& value) {
            EditorCommand command{EditorCommandType::SetModuleParameter};
            command.text = descriptor.id;
            command.moduleParameter.type = static_cast<int>(value.type);
            command.moduleParameter.boolean = value.boolean;
            command.moduleParameter.integer = value.integer;
            command.moduleParameter.number = value.number;
            command.moduleParameter.text = value.text;
            command.color = {value.color.x, value.color.y, value.color.z};
            editorSession_.request(std::move(command));
        };

        if (EditorUi::section("Module parameters", true)) {
            for (const ModuleParameterDescriptor& descriptor
                     : moduleRuntime_.parameters().descriptors()) {
                const ModuleParameterValue* current =
                    moduleRuntime_.parameters().value(descriptor.id);
                if (current == nullptr) continue;
                const char* label = descriptor.displayName.c_str();
                switch (descriptor.type) {
                    case ModuleParameterType::Bool: {
                        bool value = current->boolean;
                        if (EditorUi::Checkbox(label, &value)) {
                            ModuleParameterValue next = *current;
                            next.boolean = value;
                            requestParameter(descriptor, next);
                        }
                        break;
                    }
                    case ModuleParameterType::Int: {
                        int value = current->integer;
                        if (EditorUi::SliderInt(
                                label, &value,
                                static_cast<int>(descriptor.minimum),
                                static_cast<int>(descriptor.maximum)
                            )) {
                            ModuleParameterValue next = *current;
                            next.integer = value;
                            requestParameter(descriptor, next);
                        }
                        break;
                    }
                    case ModuleParameterType::Float: {
                        float value = current->number;
                        if (EditorUi::SliderFloat(
                                label, &value,
                                static_cast<float>(descriptor.minimum),
                                static_cast<float>(descriptor.maximum),
                                "%.3f"
                            )) {
                            ModuleParameterValue next = *current;
                            next.number = value;
                            requestParameter(descriptor, next);
                        }
                        break;
                    }
                    case ModuleParameterType::Color: {
                        glm::vec3 value = current->color;
                        if (EditorUi::ColorEdit3(label, &value.x)) {
                            ModuleParameterValue next = *current;
                            next.color = value;
                            requestParameter(descriptor, next);
                        }
                        break;
                    }
                    case ModuleParameterType::Enum: {
                        std::vector<const char*> labels;
                        labels.reserve(descriptor.enumLabels.size());
                        for (const std::string& entry : descriptor.enumLabels) {
                            labels.push_back(entry.c_str());
                        }
                        if (labels.empty()) break;
                        int value = std::clamp(
                            current->integer, 0, static_cast<int>(labels.size()) - 1
                        );
                        if (EditorUi::Combo(label, &value, labels.data(), static_cast<int>(labels.size()))) {
                            ModuleParameterValue next = *current;
                            next.integer = value;
                            next.text = descriptor.enumLabels[static_cast<std::size_t>(value)];
                            requestParameter(descriptor, next);
                        }
                        break;
                    }
                    case ModuleParameterType::Asset: {
                        // Asset picking needs the native file dialog slice; showing the
                        // stored path keeps the panel honest in the meantime.
                        EditorUi::propertyRow(label, [&](const char*) {
                            ImGui::TextDisabled(
                                "%s", current->text.empty() ? "(unset)" : current->text.c_str()
                            );
                            return false;
                        });
                        break;
                    }
                }
            }
        }

        if (EditorUi::section("Module log")) {
            const std::vector<ModuleLogEntry>& entries = moduleRuntime_.logEntries();
            if (entries.empty()) {
                ImGui::TextDisabled("No module output.");
            }
            for (const ModuleLogEntry& entry : entries) {
                ImGui::TextDisabled(
                    "[%s] frame %d: %s",
                    moduleLogSeverityName(entry.severity),
                    entry.frame,
                    entry.message.c_str()
                );
            }
        }
    }
}

void Application::drawAssetsPanel() {
    ImGui::SetNextWindowSizeConstraints(
        EditorUi::minimumDockedPanelSize,
        ImVec2(FLT_MAX, FLT_MAX)
    );
    if (!ImGui::Begin(EditorUi::label("Workspace###Workspace"))) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("WorkspaceTabs")) {
        const ImGuiTabItemFlags assetsTabFlags = focusAssetsTab_
            ? ImGuiTabItemFlags_SetSelected
            : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Assets", nullptr, assetsTabFlags)) {
            focusAssetsTab_ = false;
            updateAssetThumbnail();
            const auto queueAssetAction = [&](const WorkspaceAssetRecord& asset) {
                EditorCommand command;
                switch (asset.category) {
                    case WorkspaceAssetCategory::Scenes:
                        command.type = EditorCommandType::OpenSceneAsset;
                        break;
                    case WorkspaceAssetCategory::Models:
                        command.type = EditorCommandType::ImportModelAsset;
                        break;
                    case WorkspaceAssetCategory::RenderJobs:
                        command.type = EditorCommandType::SelectRenderJobAsset;
                        break;
                    default:
                        return;
                }
                command.text = asset.path.u8string();
                editorSession_.request(std::move(command));
            };
            const auto hasAssetAction = [](WorkspaceAssetCategory category) {
                return category == WorkspaceAssetCategory::Scenes
                    || category == WorkspaceAssetCategory::Models
                    || category == WorkspaceAssetCategory::RenderJobs;
            };

            ImGui::SetNextItemWidth(std::min(300.0f, ImGui::GetContentRegionAvail().x * 0.45f));
            ImGui::InputTextWithHint("##ContentSearch", "Search assets...",
                                     contentSearch_.data(), contentSearch_.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) {
                editorSession_.request(EditorCommand{EditorCommandType::RefreshAssetCatalog});
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu assets | cache #%llu", workspaceAssets_.records().size(),
                static_cast<unsigned long long>(thumbnailCacheGeneration_));

            if (ImGui::BeginTabBar("ContentCategories", ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (int index = 0; index < static_cast<int>(WorkspaceAssetCategory::Count); ++index) {
                    const auto category = static_cast<WorkspaceAssetCategory>(index);
                    const std::string categoryLabel = std::string(workspaceAssetCategoryName(category))
                        + " (" + std::to_string(workspaceAssets_.count(category)) + ")##AssetCategory"
                        + std::to_string(index);
                    if (ImGui::BeginTabItem(categoryLabel.c_str())) {
                        if (contentCategory_ != index) contentExtensionFilter_.clear();
                        contentCategory_ = index;
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            const auto activeCategory = static_cast<WorkspaceAssetCategory>(contentCategory_);
            const auto extensions = workspaceAssets_.extensions(activeCategory);
            ImGui::SetNextItemWidth(135.0f);
            const char* extensionPreview = contentExtensionFilter_.empty()
                ? "All types" : contentExtensionFilter_.c_str();
            if (ImGui::BeginCombo("##AssetExtension", extensionPreview)) {
                if (ImGui::Selectable("All types", contentExtensionFilter_.empty())) {
                    contentExtensionFilter_.clear();
                }
                for (const std::string& extension : extensions) {
                    if (ImGui::Selectable(extension.c_str(), contentExtensionFilter_ == extension)) {
                        contentExtensionFilter_ = extension;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const char* sortPreview = contentSortMode_ == 0 ? "Name" : "Size";
            if (ImGui::BeginCombo("##AssetSort", sortPreview)) {
                if (ImGui::Selectable("Name", contentSortMode_ == 0)) contentSortMode_ = 0;
                if (ImGui::Selectable("Size", contentSortMode_ == 1)) contentSortMode_ = 1;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(contentGridView_ ? "Grid: On" : "Grid: Off")) {
                contentGridView_ = !contentGridView_;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Search / type / %s", sortPreview);

            const auto visibleAssets = workspaceAssets_.filter(
                activeCategory,
                contentSearch_.data(),
                contentExtensionFilter_,
                contentSortMode_ == 0 ? WorkspaceAssetSort::Name : WorkspaceAssetSort::Size
            );
            if (contentGridView_) {
                for (const WorkspaceAssetRecord* asset : visibleAssets) {
                    if (isPreviewableAsset(asset->category)
                        && uploadedThumbnails_.find(asset->path) == uploadedThumbnails_.end()) {
                        requestAssetThumbnail(*asset);
                        break;
                    }
                }
            }
            const float reservedHeight = activeCategory == WorkspaceAssetCategory::Models
                ? 178.0f : 112.0f;
            const float resultHeight = std::max(130.0f,
                ImGui::GetContentRegionAvail().y - reservedHeight);
            if (ImGui::BeginChild("AssetResults", ImVec2(0.0f, resultHeight), true)) {
                if (visibleAssets.empty()) {
                    ImGui::TextDisabled("No matching %s assets.",
                        workspaceAssetCategoryName(activeCategory));
                } else if (contentGridView_) {
                    const float cardWidth = 155.0f;
                    const int columns = std::max(1,
                        static_cast<int>(ImGui::GetContentRegionAvail().x / cardWidth));
                    if (ImGui::BeginTable("AssetGrid", columns,
                            ImGuiTableFlags_SizingStretchSame)) {
                        for (const WorkspaceAssetRecord* asset : visibleAssets) {
                            ImGui::TableNextColumn();
                            ImGui::PushID(asset->relativePath.generic_u8string().c_str());
                            const bool selected = selectedWorkspaceAsset_ == asset->path;
                            const auto thumbnail = uploadedThumbnails_.find(asset->path);
                            if (thumbnail != uploadedThumbnails_.end()
                                && thumbnail->second.texture != 0U) {
                                ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(
                                    thumbnail->second.texture)), ImVec2(128.0f, 80.0f));
                            } else {
                                ImGui::InvisibleButton("##ThumbnailPlaceholder", ImVec2(128.0f, 80.0f));
                                const ImVec2 top = ImGui::GetItemRectMin();
                                const ImVec2 bottom = ImGui::GetItemRectMax();
                                ImGui::GetWindowDrawList()->AddRectFilled(top, bottom,
                                    IM_COL32(40, 49, 62, 255));
                                ImGui::GetWindowDrawList()->AddText(
                                    ImVec2(top.x + 9.0f, top.y + 31.0f), IM_COL32(196, 207, 221, 255),
                                    thumbnail != uploadedThumbnails_.end()
                                        ? "UNAVAILABLE" : workspaceAssetCategoryBadge(asset->category));
                                if (thumbnail != uploadedThumbnails_.end()
                                    && !thumbnail->second.error.empty() && ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("%s", thumbnail->second.error.c_str());
                                }
                            }
                            if (selected) ImGui::GetWindowDrawList()->AddRect(
                                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                IM_COL32(113, 183, 255, 255), 0.0f, 0, 2.0f);
                            if (ImGui::IsItemClicked()) {
                                selectedWorkspaceAsset_ = asset->path;
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) queueAssetAction(*asset);
                            }
                            const std::string cardLabel = asset->displayName + "##Card";
                            if (ImGui::Selectable(cardLabel.c_str(), selected)) {
                                selectedWorkspaceAsset_ = asset->path;
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                    queueAssetAction(*asset);
                                }
                            }
                            ImGui::TextDisabled("%s", formatAssetSize(asset->sizeBytes).c_str());
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                } else if (ImGui::BeginTable("AssetList", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 210.0f);
                    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                    ImGui::TableHeadersRow();
                    for (const WorkspaceAssetRecord* asset : visibleAssets) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(workspaceAssetCategoryBadge(asset->category));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(asset->relativePath.generic_u8string().c_str());
                        if (ImGui::Selectable(asset->displayName.c_str(),
                                selectedWorkspaceAsset_ == asset->path,
                                ImGuiSelectableFlags_SpanAllColumns)) {
                            selectedWorkspaceAsset_ = asset->path;
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                queueAssetAction(*asset);
                            }
                        }
                        ImGui::PopID();
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(asset->relativePath.generic_u8string().c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(formatAssetSize(asset->sizeBytes).c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }

            ImGui::SeparatorText(EditorUi::label("Asset details"));
            const WorkspaceAssetRecord* selectedAsset = workspaceAssets_.find(selectedWorkspaceAsset_);
            if (selectedAsset != nullptr) {
                ImGui::Text("%s | %s", workspaceAssetCategoryBadge(selectedAsset->category),
                            selectedAsset->displayName.c_str());
                ImGui::TextDisabled("%s | %s | preview %016llx",
                    selectedAsset->relativePath.generic_u8string().c_str(),
                    formatAssetSize(selectedAsset->sizeBytes).c_str(),
                    static_cast<unsigned long long>(selectedAsset->previewCacheKey));
                ImGui::SameLine();
                ImGui::BeginDisabled(!hasAssetAction(selectedAsset->category));
                const char* actionLabel = selectedAsset->category == WorkspaceAssetCategory::Scenes
                    ? "Open Scene" : selectedAsset->category == WorkspaceAssetCategory::Models
                    ? "Import Model" : selectedAsset->category == WorkspaceAssetCategory::RenderJobs
                    ? "Send to Render Queue" : "Read-only metadata";
                if (ImGui::Button(actionLabel)) queueAssetAction(*selectedAsset);
                ImGui::EndDisabled();
            } else {
                ImGui::TextDisabled("Select an asset to inspect its path, size and preview cache key.");
            }

            if (activeCategory == WorkspaceAssetCategory::Models) {
                ImGui::SeparatorText(EditorUi::label("Import external model"));
                ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - 260.0f));
                ImGui::InputText("##ModelPath", modelPathBuffer_.data(), modelPathBuffer_.size());
                ImGui::SameLine();
                const bool loadInProgress = pendingModelImport_.has_value();
                ImGui::BeginDisabled(loadInProgress);
                if (ImGui::Button(EditorUi::label("Browse..."))) {
                    std::string dialogError;
                    const auto selected = openModelFileDialog(dialogError);
                    if (selected.has_value()) {
                        const std::string selectedPath = selected->string();
                        std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(),
                                      "%s", selectedPath.c_str());
                        EditorCommand command{EditorCommandType::ImportModelAsset};
                        command.text = selected->u8string();
                        editorSession_.request(std::move(command));
                    } else if (!dialogError.empty()) {
                        statusMessage_ = "Open failed: " + dialogError;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(EditorUi::label("Load entered path"))) {
                    EditorCommand command{EditorCommandType::ImportModelAsset};
                    command.text = std::filesystem::u8path(modelPathBuffer_.data()).u8string();
                    editorSession_.request(std::move(command));
                }
                ImGui::EndDisabled();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Timeline")) {
            int startFrame = editorSession_.startFrame();
            int endFrame = editorSession_.endFrame();
            int fps = editorSession_.framesPerSecond();
            int frame = editorSession_.frame();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputInt("Start", &startFrame)) editorSession_.setFrameRange(startFrame, endFrame);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputInt("End", &endFrame)) editorSession_.setFrameRange(startFrame, endFrame);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("FPS", &fps)) editorSession_.setFramesPerSecond(fps);
            if (ImGui::SliderInt("Frame", &frame, editorSession_.startFrame(), editorSession_.endFrame())) {
                editorSession_.setFrame(frame);
                animationTimeFixed_ = true;
                animationTimeSeconds_ = static_cast<float>(editorSession_.timeSeconds());
                cpuPreviewRestartRequested_ = true;
            }
            ImGui::TextDisabled("Time %.3f s | deterministic fixed step %.6f s",
                editorSession_.timeSeconds(), 1.0 / static_cast<double>(editorSession_.framesPerSecond()));
            ImGui::TextWrapped("The P1-0A timeline owns editor frame semantics. Cache baking and sequence evaluation are connected in P1-0B/C.");
            ImGui::EndTabItem();
        }

        const ImGuiTabItemFlags modulesTabFlags = focusModulesTab_
            ? ImGuiTabItemFlags_SetSelected
            : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Modules", nullptr, modulesTabFlags)) {
            focusModulesTab_ = false;
            const std::vector<ModuleManifest> manifests = moduleRegistry_.manifests();
            ImGui::TextUnformatted("Statically linked module registry");
            ImGui::TextDisabled(
                "%zu module(s) | Module API %d | Build %s",
                manifests.size(),
                moduleApiVersion,
                moduleBuildId().c_str()
            );
            if (ImGui::BeginTable(
                    "ModuleTable",
                    5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX
                )) {
                // Fixed widths plus horizontal scrolling keep every label readable at the
                // 1100x680 minimum window instead of clipping the last columns; the full
                // value is also available as a hover tooltip.
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Module ID", ImGuiTableColumnFlags_WidthFixed, 185.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 175.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();
                const auto cell = [](const std::string& text) {
                    ImGui::TextUnformatted(text.c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip("%s", text.c_str());
                    }
                };
                for (const ModuleManifest& manifest : manifests) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    cell(manifest.id);
                    ImGui::TableSetColumnIndex(1);
                    cell(manifest.displayName);
                    ImGui::TableSetColumnIndex(2);
                    cell(moduleKindName(manifest.kind));
                    ImGui::TableSetColumnIndex(3);
                    cell(manifest.cmakeTarget);
                    ImGui::TableSetColumnIndex(4);
                    cell(manifest.sourceRoot);
                }
                ImGui::EndTable();
            }
            ImGui::TextWrapped(
                "The panel and the Content Browser only read module manifests; no C++ source is "
                "scanned or parsed. Instance lifecycle, parameter controls and module build "
                "diagnostics arrive with the C1 runtime wiring."
            );
            ImGui::EndTabItem();
        }

        const ImGuiTabItemFlags renderQueueTabFlags = focusRenderQueueTab_
            ? ImGuiTabItemFlags_SetSelected
            : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Render Queue", nullptr, renderQueueTabFlags)) {
            focusRenderQueueTab_ = false;
            ImGui::SetNextItemWidth(std::max(240.0f, ImGui::GetContentRegionAvail().x - 230.0f));
            ImGui::InputText("##RenderJobPath", renderJobPathBuffer_.data(), renderJobPathBuffer_.size());
            ImGui::SameLine();
            if (ImGui::Button("Browse...##RenderJob")) {
                std::string dialogError;
                const auto selected = openRenderJobFileDialog(dialogError);
                if (selected.has_value()) {
                    const std::string selectedPath = selected->string();
                    std::snprintf(renderJobPathBuffer_.data(), renderJobPathBuffer_.size(),
                                  "%s", selectedPath.c_str());
                } else if (!dialogError.empty()) {
                    renderQueueMessage_ = dialogError;
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(renderJobPathBuffer_[0] == '\0');
            if (ImGui::Button("Enqueue Sequence")) {
                EditorCommand command{EditorCommandType::SubmitRenderJob};
                command.text = renderJobPathBuffer_.data();
                editorSession_.request(std::move(command));
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            const auto queueEntries = renderQueue_->entries();
            if (!queueEntries.empty() && ImGui::BeginTable(
                    "RenderQueueTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                    ImVec2(0.0f, 128.0f))) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
                ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("State / Progress", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 176.0f);
                ImGui::TableHeadersRow();
                for (const RenderQueueEntrySnapshot& entry : queueEntries) {
                    ImGui::PushID(static_cast<int>(entry.id));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu", static_cast<unsigned long long>(entry.id));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(entry.jobPath.filename().string().c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.jobPath.string().c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(renderQueueStatusName(entry.status));
                    ImGui::SameLine();
                    const int total = entry.endFrame - entry.startFrame + 1;
                    const float fraction = total > 0
                        ? static_cast<float>(entry.completedFrames) / static_cast<float>(total)
                        : 0.0f;
                    const std::string progressLabel = std::to_string(entry.completedFrames)
                        + "/" + std::to_string(total);
                    ImGui::ProgressBar(fraction, ImVec2(-1.0f, ImGui::GetFrameHeight()), progressLabel.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d-%d @ %d FPS", entry.startFrame, entry.endFrame,
                                entry.framesPerSecond);
                    ImGui::TableSetColumnIndex(4);
                    if (entry.status == RenderQueueStatus::Pending) {
                        if (ImGui::SmallButton("Up")) editorSession_.request(EditorCommand{
                            EditorCommandType::MoveRenderJobUp, entry.id});
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Down")) editorSession_.request(EditorCommand{
                            EditorCommandType::MoveRenderJobDown, entry.id});
                        ImGui::SameLine();
                    }
                    if (entry.status == RenderQueueStatus::Running) {
                        if (ImGui::SmallButton("Cancel")) editorSession_.request(EditorCommand{
                            EditorCommandType::CancelRenderJob, entry.id});
                    } else if (entry.status == RenderQueueStatus::Cancelling) {
                        ImGui::TextDisabled("Cancelling...");
                    } else {
                        if (entry.status == RenderQueueStatus::Failed
                            || entry.status == RenderQueueStatus::Cancelled) {
                            if (ImGui::SmallButton("Retry")) editorSession_.request(EditorCommand{
                                EditorCommandType::RetryRenderJob, entry.id});
                            ImGui::SameLine();
                        }
                        if (ImGui::SmallButton("Remove")) editorSession_.request(EditorCommand{
                            EditorCommandType::RemoveRenderJob, entry.id});
                    }
                    if (!entry.message.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", entry.message.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            } else if (queueEntries.empty()) {
                ImGui::TextDisabled("Queue is empty.");
            }
            ImGui::TextWrapped("%s", renderQueueMessage_.c_str());
            ImGui::TextDisabled("Jobs run serially. Pending order and results persist across restarts.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Log / Profile",
                nullptr,
                focusLogTab_ ? ImGuiTabItemFlags_SetSelected : 0
            )) {
            focusLogTab_ = false;
            drawLogProfilePanel();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Application::rebuildSceneEntities() {
    stressEntities_.clear();
    editedEntities_.clear();
    scene_.clear();
    primaryEntity_ = invalidSceneEntityId;
    comparisonEntity_ = invalidSceneEntityId;
    backdropEntity_ = invalidSceneEntityId;
    groundEntity_ = invalidSceneEntityId;
    selectedSceneEntity_ = invalidSceneEntityId;
    foundationDemoEntities_.clear();
    if (model_ != nullptr) {
        const std::string name = currentModelPath_.empty()
            ? "Model"
            : currentModelPath_.filename().string();
        const std::string resource = currentModelPath_.generic_u8string();
        primaryEntity_ = scene_.createEntity(name, model_.get(), resource);
        comparisonEntity_ = scene_.createEntity("Comparison instance", model_.get(), resource);
        selectedSceneEntity_ = primaryEntity_;
        if (sceneFoundationDemoEnabled_) {
            for (int index = 0; index < 9; ++index) {
                foundationDemoEntities_.push_back(scene_.createEntity(
                    "Shared scene instance " + std::to_string(index + 2),
                    model_.get(),
                    resource
                ));
            }
        }
    }
    if (glassBackdropModel_ != nullptr) {
        backdropEntity_ = scene_.createEntity(
            "Glass checkerboard backdrop", glassBackdropModel_.get(), builtinGlassBackdropResource
        );
    }
    if (groundModel_ != nullptr) {
        groundEntity_ = scene_.createEntity(
            EditorUi::label("Ground receiver"), groundModel_.get(), builtinGroundResource
        );
    }
}

void Application::syncSceneEntities(const glm::mat4& normalization) {
    if (loadedSceneDocument_) {
        scene_.updateWorldTransforms();
        return;
    }
    if (SceneEntity* primary = scene_.find(primaryEntity_); primary && !editedEntities_.count(primaryEntity_)) {
        primary->model = model_.get();
        primary->transform.translation = modelPosition_;
        primary->transform.rotationDegrees = modelRotationDegrees_;
        primary->transform.scale = glm::vec3(modelScale_);
        primary->transform.assetTransform = normalization;
        primary->tint = rendererSettings_.baseColor;
        primary->enabledByPreset = !lightStressDemoEnabled_ && !instanceStressDemoEnabled_
            && (!prismDemoEnabled_ || prismModelVisible_);
        primary->castsShadow = true;
    }
    if (SceneEntity* comparison = scene_.find(comparisonEntity_); comparison && !editedEntities_.count(comparisonEntity_)) {
        comparison->model = model_.get();
        comparison->transform.translation = modelPosition_ + (glassVolumeDemoEnabled_
            ? glm::vec3(0.92f, 0.0f, 0.0f)
            : glm::vec3(0.95f, 0.0f, 0.35f));
        comparison->transform.rotationDegrees = glassVolumeDemoEnabled_
            ? glm::vec3(0.0f)
            : glm::vec3(0.0f, -28.0f, 0.0f);
        comparison->transform.scale = glm::vec3(
            modelScale_ * (glassVolumeDemoEnabled_ ? 0.88f : 0.50f)
        );
        comparison->transform.assetTransform = normalization;
        comparison->tint = glassVolumeDemoEnabled_
            ? glm::vec3(1.0f)
            : glm::vec3(0.72f, 0.82f, 1.0f);
        comparison->enabledByPreset = showComparisonObject_
            && !lightStressDemoEnabled_ && !instanceStressDemoEnabled_;
        comparison->castsShadow = true;
    }
    static constexpr std::array<glm::vec3, 6> foundationTints{
        glm::vec3(0.90f, 0.38f, 0.28f),
        glm::vec3(0.94f, 0.68f, 0.24f),
        glm::vec3(0.36f, 0.78f, 0.46f),
        glm::vec3(0.28f, 0.62f, 0.92f),
        glm::vec3(0.54f, 0.40f, 0.90f),
        glm::vec3(0.88f, 0.34f, 0.68f)
    };
    for (std::size_t index = 0; index < foundationDemoEntities_.size(); ++index) {
        if (SceneEntity* entity = scene_.find(foundationDemoEntities_[index]); entity && !editedEntities_.count(entity->id)) {
            const int slot = static_cast<int>(index) + 1;
            entity->model = model_.get();
            entity->transform.translation = glm::vec3(
                -3.0f + static_cast<float>(slot % 5) * 1.5f,
                0.0f,
                slot < 5 ? -1.05f : 1.05f
            );
            entity->transform.rotationDegrees = glm::vec3(0.0f, static_cast<float>(slot * 23), 0.0f);
            entity->transform.scale = glm::vec3(0.82f);
            entity->transform.assetTransform = normalization;
            entity->tint = foundationTints[index % foundationTints.size()];
            entity->enabledByPreset = sceneFoundationDemoEnabled_;
            entity->castsShadow = false;
            entity->instanceCandidate = false;
        }
    }
    if (SceneEntity* backdrop = scene_.find(backdropEntity_); backdrop && !editedEntities_.count(backdropEntity_)) {
        backdrop->model = glassBackdropModel_.get();
        backdrop->transform = SceneTransform{};
        backdrop->enabledByPreset = glassVolumeDemoEnabled_ && !glassCausticsDemoEnabled_;
        backdrop->castsShadow = false;
    }
    if (SceneEntity* ground = scene_.find(groundEntity_); ground && !editedEntities_.count(groundEntity_)) {
        ground->model = groundModel_.get();
        ground->transform = SceneTransform{};
        ground->transform.translation = glm::vec3(
            modelPosition_.x,
            modelPosition_.y + groundOffset_ * modelScale_,
            modelPosition_.z
        );
        ground->tint = groundColor_;
        ground->enabledByPreset = showGroundPlane_;
        ground->castsShadow = false;
    }
    scene_.updateWorldTransforms();
}

void Application::drawEditorLayout() {
    const ImGuiID dock = ImGui::DockSpaceOverViewport();
    const ImGuiDockNode* root = ImGui::DockBuilderGetNode(dock);
    const bool explicitReset = resetEditorLayout_;
    const bool layoutNeedsRepair = root == nullptr || !root->IsSplitNode()
        || hasUndersizedDockLeaf(root);
    if (explicitReset || layoutNeedsRepair) {
        resetEditorLayout_ = false;
        if (explicitReset) {
            hierarchyPanelOpen_ = true;
            inspectorPanelOpen_ = true;
            assetsPanelOpen_ = true;
        }
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        ImGui::DockBuilderSetNodeSize(dock, workSize);
        ImGuiID center = dock;
        const float leftWidth = std::max(EditorUi::minimumDockedPanelSize.x, workSize.x * 0.18f);
        const float leftRatio = std::min(leftWidth / std::max(workSize.x, 1.0f), 0.28f);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftRatio, nullptr, &center);
        const float remainingWidth = std::max(workSize.x - leftWidth, 1.0f);
        const float rightWidth = std::max(300.0f, workSize.x * 0.23f);
        const float rightRatio = std::min(rightWidth / remainingWidth, 0.38f);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightRatio, nullptr, &center);
        const float bottomRatio = workSize.y < 700.0f ? 0.43f : 0.30f;
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,
            bottomRatio, nullptr, &center);
        ImGui::DockBuilderDockWindow("###Hierarchy", left);
        ImGui::DockBuilderDockWindow("###Inspector", right);
        ImGui::DockBuilderDockWindow("###Viewport", center);
        ImGui::DockBuilderDockWindow("###Workspace", bottom);
        ImGui::DockBuilderFinish(dock);
    }
}

void Application::selectEntity(SceneEntityId id) {
    selectedSceneEntity_ = scene_.find(id) ? id : invalidSceneEntityId;
    focusObjectTab_ = true;
}

void Application::deleteSelectedEntity() {
    const auto id = selectedSceneEntity_;
    if (!scene_.destroyEntity(id)) return;
    editedEntities_.erase(id);
    if (id == primaryEntity_) primaryEntity_ = invalidSceneEntityId;
    if (id == comparisonEntity_) { comparisonEntity_ = invalidSceneEntityId; showComparisonObject_ = false; }
    if (id == groundEntity_) { groundEntity_ = invalidSceneEntityId; showGroundPlane_ = false; }
    if (id == backdropEntity_) backdropEntity_ = invalidSceneEntityId;
    selectEntity(invalidSceneEntityId);
}

void Application::newEmptyScene() {
    // In-flight CPU work may finish, but its result belongs to the old generation.
    ++sceneGeneration_;
    droppedModelPaths_.clear();
    scene_.clear();
    importedModels_.clear();
    model_.reset();
    stressEntities_.clear();
    editedEntities_.clear();
    foundationDemoEntities_.clear();
    primaryEntity_ = comparisonEntity_ = groundEntity_ = backdropEntity_ = invalidSceneEntityId;
    selectEntity(invalidSceneEntityId);
    emptySceneSession_ = true;
    loadedSceneDocument_ = false;
    prismDemoPreviousState_.reset();
    prismDemoEnabled_ = glassVolumeDemoEnabled_ = glassCausticsDemoEnabled_ = false;
    lightStressDemoEnabled_ = instanceStressDemoEnabled_ = sceneFoundationDemoEnabled_ = false;
    animationDemoEnabled_ = animationEnabled_ = false;
    temporalMotionDemoEnabled_ = objectMotionDemoEnabled_ = false;
    autoRotate_ = showGroundPlane_ = showComparisonObject_ = false;
    prismCameraLocked_ = false;
    rendererSettings_ = RendererSettings{};
    currentModelPath_.clear();
    currentScenePath_.clear();
    modelPathBuffer_.fill(0);
    modelDiagnostics_.clear();
    pendingScreenshotPath_.clear();
    loadedMeshCount_ = loadedSubmeshCount_ = loadedTransparentSubmeshCount_ = 0;
    loadedVertexCount_ = loadedTriangleCount_ = loadedMaterialCount_ = loadedTextureCount_ = 0;
    loadedDecodedTextureCount_ = loadedFallbackTextureCount_ = loadedTextureMemoryBytes_ = 0;
    modelCenter_ = glm::vec3(0.0f);
    modelNormalizationScale_ = 1.0f;
    resetObjectTransform();
    camera_.reset();    statusMessage_ = EditorUi::chinese ? "已新建空场景，可导入多个模型。" : "New empty scene. Import models to begin.";
}

SceneDocument Application::captureSceneDocument() const {
    SceneDocument document;
    document.camera = camera_.orbitState();
    document.renderer = rendererSettings_;
    document.playback.animationEnabled = animationEnabled_;
    document.playback.animationPlaying = animationPlaying_;
    document.playback.animationTimeSeconds = animationTimeSeconds_;
    document.playback.animationSpeed = animationSpeed_;
    document.playback.animationClipIndex = animationClipIndex_;
    document.playback.prismEnabled = prismDemoEnabled_;
    document.playback.prismCameraLocked = prismCameraLocked_;
    document.playback.prismPreset = prismOpticalPreset_;
    document.playback.prismParameters = prismParameters_;
    document.entities.reserve(scene_.size());
    for (const SceneEntity& source : scene_.entities()) {
        SceneDocumentEntity entity;
        entity.id = source.id;
        entity.name = source.name;
        entity.parent = source.parent;
        entity.modelResource = source.modelResource;
        if (entity.modelResource.empty() && source.model == groundModel_.get()) {
            entity.modelResource = builtinGroundResource;
        } else if (entity.modelResource.empty() && source.model == glassBackdropModel_.get()) {
            entity.modelResource = builtinGlassBackdropResource;
        } else if (entity.modelResource.empty() && source.model == model_.get()) {
            entity.modelResource = currentModelPath_.generic_u8string();
        } else if (entity.modelResource.empty() && source.model != nullptr) {
            const auto matching = std::find_if(
                scene_.entities().begin(), scene_.entities().end(),
                [&](const SceneEntity& candidate) {
                    return candidate.model == source.model && !candidate.modelResource.empty();
                }
            );
            if (matching != scene_.entities().end()) entity.modelResource = matching->modelResource;
        }
        entity.transform = source.transform;
        entity.tint = source.tint;
        entity.visible = source.visible && source.enabledByPreset;
        entity.castsShadow = source.castsShadow;
        entity.instanceCandidate = source.instanceCandidate;
        document.entities.push_back(std::move(entity));
    }
    return document;
}

bool Application::saveCurrentScene() {
    return currentScenePath_.empty() ? saveSceneAs() : saveSceneTo(currentScenePath_);
}

bool Application::saveSceneAs() {
    std::filesystem::path suggestion = currentScenePath_;
    if (suggestion.empty()) suggestion = sourceRoot_ / "assets" / "scenes" / "untitled.myscene";
    std::string dialogError;
    const auto selected = saveSceneFileDialog(suggestion, dialogError);
    if (!selected.has_value()) {
        if (!dialogError.empty()) statusMessage_ = "Save scene failed: " + dialogError;
        return false;
    }
    std::filesystem::path path = *selected;
    if (path.extension().empty()) path += myRendererSceneExtension;
    return saveSceneTo(path);
}

bool Application::saveSceneTo(const std::filesystem::path& path) {
    if (pendingModelImport_.has_value()) {
        statusMessage_ = "Wait for the current model import before saving the scene.";
        return false;
    }
    SceneDocument document = captureSceneDocument();
    for (const SceneDocumentEntity& entity : document.entities) {
        const SceneEntity* source = scene_.find(entity.id);
        if (source != nullptr && source->model != nullptr && entity.modelResource.empty()) {
            statusMessage_ = "Save scene failed: entity '" + entity.name + "' has no persistent model resource.";
            return false;
        }
    }
    std::error_code pathError;
    const std::filesystem::path absolute = std::filesystem::absolute(path, pathError).lexically_normal();
    std::string error;
    if (pathError || !saveSceneDocument(pathError ? path : absolute, document, error)) {
        statusMessage_ = "Save scene failed: " + (pathError ? pathError.message() : error);
        return false;
    }
    currentScenePath_ = absolute;
    rememberRecentScene(absolute);
    statusMessage_ = "Saved scene " + absolute.filename().u8string();
    std::cout << statusMessage_ << " (" << document.entities.size() << " entities)\n";
    return true;
}

void Application::openSceneFromDialog() {
    if (pendingModelImport_.has_value()) return;
    std::string dialogError;
    const auto selected = openSceneFileDialog(dialogError);
    if (selected.has_value()) {
        openScene(*selected);
    } else if (!dialogError.empty()) {
        statusMessage_ = "Open scene failed: " + dialogError;
    }
}

bool Application::openScene(const std::filesystem::path& path) {
    if (pendingModelImport_.has_value()) {
        statusMessage_ = "Wait for the current model import before opening a scene.";
        return false;
    }
    std::error_code pathError;
    const std::filesystem::path absolute = std::filesystem::absolute(path, pathError).lexically_normal();
    if (pathError) {
        statusMessage_ = "Open scene failed: " + pathError.message();
        return false;
    }
    SceneDocument document;
    std::string documentError;
    if (!loadSceneDocument(absolute, document, documentError)) {
        statusMessage_ = "Open scene failed: " + documentError;
        return false;
    }

    struct PreparedModel {
        std::filesystem::path path;
        std::unique_ptr<GpuModel> model;
    };
    std::vector<PreparedModel> prepared;
    std::unordered_map<std::string, const GpuModel*> modelsByPath;
    std::unordered_map<std::string, std::string> resolvedResources;
    try {
        for (const SceneDocumentEntity& entity : document.entities) {
            if (entity.modelResource.empty()
                || entity.modelResource == builtinGroundResource
                || entity.modelResource == builtinGlassBackdropResource) {
                continue;
            }
            const std::filesystem::path resolved = resolveSceneResource(entity.modelResource, absolute);
            const std::string key = resolved.generic_u8string();
            resolvedResources[entity.modelResource] = key;
            if (modelsByPath.count(key) != 0U) continue;
            if (!std::filesystem::is_regular_file(resolved)) {
                throw std::runtime_error("Missing model resource: " + resolved.string());
            }
            const ModelImporter* importer = findImporter(resolved);
            if (importer == nullptr) throw std::runtime_error("Unsupported model resource: " + resolved.string());
            ModelImportResult imported = importer->load(resolved);
            std::vector<TextureUploadWarning> warnings;
            auto gpu = std::make_unique<GpuModel>(
                std::move(imported.model), renderer_->textureCache(), warnings
            );
            const GpuModel* pointer = gpu.get();
            prepared.push_back(PreparedModel{resolved, std::move(gpu)});
            modelsByPath.emplace(key, pointer);
        }

        newEmptyScene();
        currentScenePath_ = absolute;
        loadedSceneDocument_ = true;
        emptySceneSession_ = false;
        rendererSettings_ = document.renderer;
        camera_.setOrbitState(document.camera);
        animationEnabled_ = document.playback.animationEnabled;
        animationPlaying_ = document.playback.animationPlaying;
        animationTimeSeconds_ = document.playback.animationTimeSeconds;
        animationSpeed_ = document.playback.animationSpeed;
        animationClipIndex_ = document.playback.animationClipIndex;
        prismDemoEnabled_ = document.playback.prismEnabled;
        prismCameraLocked_ = document.playback.prismCameraLocked;
        prismOpticalPreset_ = document.playback.prismPreset;
        prismParameters_ = document.playback.prismParameters;

        if (!prepared.empty()) {
            currentModelPath_ = prepared.front().path;
            model_ = std::move(prepared.front().model);
            for (std::size_t index = 1; index < prepared.size(); ++index) {
                importedModels_.push_back(std::move(prepared[index].model));
            }
        }
        const auto modelFor = [&](const std::string& resource) -> const GpuModel* {
            if (resource.empty()) return nullptr;
            if (resource == builtinGroundResource) return groundModel_.get();
            if (resource == builtinGlassBackdropResource) return glassBackdropModel_.get();
            const auto resolved = resolvedResources.find(resource);
            if (resolved == resolvedResources.end()) return nullptr;
            const auto found = modelsByPath.find(resolved->second);
            return found == modelsByPath.end() ? nullptr : found->second;
        };
        for (const SceneDocumentEntity& saved : document.entities) {
            const std::string persistentResource = saved.modelResource.rfind("builtin:", 0U) == 0U
                ? saved.modelResource
                : resolvedResources[saved.modelResource];
            const SceneEntityId id = scene_.createEntityWithId(
                saved.id, saved.name, modelFor(saved.modelResource), persistentResource
            );
            if (id == invalidSceneEntityId) throw std::runtime_error("Could not restore scene entity ID");
            SceneEntity* entity = scene_.find(id);
            entity->transform = saved.transform;
            entity->tint = saved.tint;
            entity->visible = saved.visible;
            entity->enabledByPreset = true;
            entity->castsShadow = saved.castsShadow;
            entity->instanceCandidate = saved.instanceCandidate;
            editedEntities_.insert(id);
        }
        for (const SceneDocumentEntity& saved : document.entities) {
            if (saved.parent != invalidSceneEntityId && !scene_.setParent(saved.id, saved.parent)) {
                throw std::runtime_error("Could not restore scene hierarchy");
            }
        }
        scene_.updateWorldTransforms();
        selectedSceneEntity_ = document.entities.empty()
            ? invalidSceneEntityId
            : document.entities.front().id;
        showGroundPlane_ = std::any_of(
            document.entities.begin(), document.entities.end(),
            [](const SceneDocumentEntity& entity) {
                return entity.modelResource == builtinGroundResource && entity.visible;
            }
        );

        loadedMeshCount_ = loadedSubmeshCount_ = loadedTransparentSubmeshCount_ = 0U;
        loadedVertexCount_ = loadedTriangleCount_ = loadedMaterialCount_ = loadedTextureCount_ = 0U;
        loadedDecodedTextureCount_ = loadedFallbackTextureCount_ = loadedTextureMemoryBytes_ = 0U;
        std::unordered_set<const GpuModel*> counted;
        for (const SceneEntity& entity : scene_.entities()) {
            if (entity.model == nullptr || entity.model == groundModel_.get()
                || entity.model == glassBackdropModel_.get() || !counted.insert(entity.model).second) continue;
            loadedMeshCount_ += entity.model->meshCount();
            loadedSubmeshCount_ += entity.model->submeshCount();
            loadedTransparentSubmeshCount_ += entity.model->transparentSubmeshCount();
            loadedVertexCount_ += entity.model->vertexCount();
            loadedTriangleCount_ += entity.model->triangleCount();
            loadedMaterialCount_ += entity.model->materialCount();
            loadedTextureCount_ += entity.model->textureCount();
            loadedDecodedTextureCount_ += entity.model->loadedTextureCount();
            loadedFallbackTextureCount_ += entity.model->fallbackTextureCount();
            loadedTextureMemoryBytes_ += entity.model->textureMemoryBytes();
        }
        if (!currentModelPath_.empty()) {
            const std::string modelPath = currentModelPath_.string();
            std::snprintf(modelPathBuffer_.data(), modelPathBuffer_.size(), "%s", modelPath.c_str());
        }
        if (prismDemoEnabled_) updatePrismDemoOptics();
        if (model_ != nullptr && model_->hasSkinning()) {
            animationClipIndex_ = std::min(
                animationClipIndex_,
                model_->animationCount() > 0U ? model_->animationCount() - 1U : 0U
            );
            model_->updateAnimation(animationEnabled_, animationClipIndex_, animationTimeSeconds_);
        }
        rememberRecentScene(absolute);
        statusMessage_ = "Opened scene " + absolute.filename().u8string() + " ("
            + std::to_string(scene_.size()) + " entities, "
            + std::to_string(counted.size()) + " model assets)";
        std::cout << statusMessage_ << '\n';
        return true;
    } catch (const std::exception& exception) {
        statusMessage_ = "Open scene failed; current scene preserved: " + std::string(exception.what());
        std::cerr << statusMessage_ << '\n';
        return false;
    }
}

void Application::rememberRecentScene(const std::filesystem::path& path) {
    std::ofstream stream("MyRenderer.recent-scene", std::ios::binary | std::ios::trunc);
    if (stream) stream << path.generic_u8string();
}

std::filesystem::path Application::recentScenePath() const {
    std::ifstream stream("MyRenderer.recent-scene", std::ios::binary);
    std::string value;
    std::getline(stream, value);
    return value.empty() ? std::filesystem::path{} : std::filesystem::u8path(value);
}

void Application::materializeStressEntities(std::vector<RenderItem>& items) {
    // Benchmark mode keeps its original synthetic submission path.
    if (stressEntities_.empty()) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto id = scene_.createEntity("Stress object " + std::to_string(i + 1), items[i].model);
            stressEntities_.push_back(id);
            auto* entity = scene_.find(id);
            if (items[i].model == model_.get()) {
                entity->modelResource = currentModelPath_.generic_u8string();
            } else {
                const auto source = std::find_if(
                    scene_.entities().begin(), scene_.entities().end(),
                    [&](const SceneEntity& candidate) {
                        return candidate.model == items[i].model && !candidate.modelResource.empty();
                    }
                );
                if (source != scene_.entities().end()) entity->modelResource = source->modelResource;
            }
            const auto& matrix = items[i].modelMatrix;
            entity->transform.translation = glm::vec3(matrix[3]);
            entity->transform.scale = glm::vec3(glm::length(glm::vec3(matrix[0])),
                glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
            const glm::mat3 rotation{glm::vec3(matrix[0]) / entity->transform.scale.x,
                glm::vec3(matrix[1]) / entity->transform.scale.y,
                glm::vec3(matrix[2]) / entity->transform.scale.z};
            entity->transform.rotationDegrees = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
            entity->tint = items[i].tint;
            entity->castsShadow = items[i].castsShadow;
            entity->instanceCandidate = items[i].instanceCandidate;
        }
    }
    scene_.updateWorldTransforms();
    items.clear();
    for (auto id : stressEntities_) {
        const auto* entity = scene_.find(id);
        if (!entity || !entity->visible) continue;
        items.push_back(RenderItem{entity->model, entity->worldTransform, entity->tint,
            true, entity->castsShadow, entity->instanceCandidate, id,
            entity->previousWorldTransform, entity->motionHistoryValid});
    }
}

SceneEntityId Application::pickEntity(const std::vector<RenderItem>& items,
    int width, int height, int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return invalidSceneEntityId;
    if (!pickingShader_) pickingShader_ = std::make_unique<Shader>(
        sourceRoot_ / "shaders/basic.vert", sourceRoot_ / "shaders/editor_pick.frag");
    // Only run on clicks; integer IDs are unaffected by lighting, tone mapping or MSAA.
    GLint previousDraw, previousRead, viewport[4], scissor[4], depthFunc, program, vao;
    GLint cullFace, frontFace, polygon[2], renderbuffer, packAlignment;
    GLboolean depthMask, colorMask[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc); glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFace); glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glGetIntegerv(GL_POLYGON_MODE, polygon); glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE), scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLuint framebuffer = 0, color = 0, depthBuffer = 0;
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenRenderbuffers(1, &color); glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_R32UI, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glGenRenderbuffers(1, &depthBuffer); glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    GLuint hit = 0;
    if (complete) {
        glViewport(0, 0, width, height);
        glEnable(GL_SCISSOR_TEST); glScissor(x, y, 1, 1);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glDisable(GL_BLEND); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glFrontFace(GL_CCW);
        const GLuint zero[4]{}; const float farDepth = 1.0f;
        glClearBufferuiv(GL_COLOR, 0, zero); glClearBufferfv(GL_DEPTH, 0, &farDepth);
        pickingShader_->use();
        pickingShader_->setMat4("uView", camera_.viewMatrix());
        pickingShader_->setMat4("uProjection", camera_.projectionMatrix(static_cast<float>(width) / height));
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (!item.visible || !item.model) continue;
            pickingShader_->setInt("uPickIndex", static_cast<int>(i + 1));
            pickingShader_->setMat4("uModel", item.modelMatrix);
            item.model->drawOpaque(*pickingShader_, glm::vec3(1.0f), rendererSettings_.cullBackFaces);
            for (std::size_t mesh = 0; mesh < item.model->transparentSubmeshCount(); ++mesh)
                item.model->drawTransparentSubmesh(*pickingShader_, glm::vec3(1.0f), mesh, rendererSettings_.cullBackFaces);
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0); glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &hit);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDraw); glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glDeleteRenderbuffers(1, &color); glDeleteRenderbuffers(1, &depthBuffer); glDeleteFramebuffers(1, &framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    const auto restore = [](GLenum cap, GLboolean enabled) { if (enabled) glEnable(cap); else glDisable(cap); };
    restore(GL_DEPTH_TEST, depth); restore(GL_BLEND, blend); restore(GL_CULL_FACE, cull); restore(GL_SCISSOR_TEST, scissorEnabled);
    glDepthFunc(depthFunc); glDepthMask(depthMask); glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glCullFace(cullFace); glFrontFace(frontFace); glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
    glUseProgram(program); glBindVertexArray(vao); glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    if (!complete) throw std::runtime_error("Editor picking framebuffer is incomplete");
    return hit > 0 && hit <= items.size() ? items[hit - 1].entityId : invalidSceneEntityId;
}

bool Application::editorInteractionRegression() {
    try {
        const auto check = [](bool passed, const char* message) { if (!passed) throw std::runtime_error(message); };
        newEmptyScene();
        const auto cube = sourceRoot_ / "assets/models/cube.obj";
        finishModelLoad(cube, findImporter(cube)->load(cube), true);
        check(scene_.size() == 1 && !model_ && !lightStressDemoEnabled_, "empty scene first import");
        scene_.updateWorldTransforms();
        const auto first = selectedSceneEntity_;
        const SceneTransform originalEditorTransform = scene_.find(first)->transform;
        const glm::vec3 originalEditorTint = scene_.find(first)->tint;
        const bool originalEditorVisibility = scene_.find(first)->visible;
        const bool originalEditorCastsShadow = scene_.find(first)->castsShadow;
        EditorCommand transformCommand{EditorCommandType::SetEntityTransform, first};
        transformCommand.transform.translation = {0.25f, 0.5f, -0.75f};
        transformCommand.transform.rotationDegrees = {10.0f, 20.0f, 30.0f};
        transformCommand.transform.scale = {0.8f, 1.2f, 1.4f};
        editorSession_.request(std::move(transformCommand));
        EditorCommand tintCommand{EditorCommandType::SetEntityTint, first};
        tintCommand.color = {0.2f, 0.4f, 0.8f};
        editorSession_.request(std::move(tintCommand));
        editorSession_.request(EditorCommand{
            EditorCommandType::SetEntityVisibility, first, 0U, false
        });
        editorSession_.request(EditorCommand{
            EditorCommandType::SetEntityCastsShadow, first, 0U, false
        });
        processEditorCommands();
        check(std::abs(scene_.find(first)->transform.translation.x - 0.25f) < 1.0e-6f
              && std::abs(scene_.find(first)->transform.rotationDegrees.y - 20.0f) < 1.0e-6f
              && std::abs(scene_.find(first)->transform.scale.z - 1.4f) < 1.0e-6f,
              "Inspector transform command did not update the Scene entity");
        check(std::abs(scene_.find(first)->tint.z - 0.8f) < 1.0e-6f,
              "Inspector tint command did not update the Scene entity");
        check(!scene_.find(first)->visible && !scene_.find(first)->castsShadow,
              "Inspector rendering commands did not update the Scene entity");

        EditorCommand restoreTransform{EditorCommandType::SetEntityTransform, first};
        restoreTransform.transform.translation = {
            originalEditorTransform.translation.x,
            originalEditorTransform.translation.y,
            originalEditorTransform.translation.z
        };
        restoreTransform.transform.rotationDegrees = {
            originalEditorTransform.rotationDegrees.x,
            originalEditorTransform.rotationDegrees.y,
            originalEditorTransform.rotationDegrees.z
        };
        restoreTransform.transform.scale = {
            originalEditorTransform.scale.x,
            originalEditorTransform.scale.y,
            originalEditorTransform.scale.z
        };
        editorSession_.request(std::move(restoreTransform));
        EditorCommand restoreTint{EditorCommandType::SetEntityTint, first};
        restoreTint.color = {originalEditorTint.x, originalEditorTint.y, originalEditorTint.z};
        editorSession_.request(std::move(restoreTint));
        editorSession_.request(EditorCommand{
            EditorCommandType::SetEntityVisibility, first, 0U, originalEditorVisibility
        });
        editorSession_.request(EditorCommand{
            EditorCommandType::SetEntityCastsShadow, first, 0U, originalEditorCastsShadow
        });
        processEditorCommands();

        const SceneEntityId child = scene_.createEntity("Hierarchy child");
        const SceneEntityId grandchild = scene_.createEntity("Hierarchy grandchild");
        check(scene_.setParent(grandchild, child), "test hierarchy setup failed");
        scene_.find(child)->motionHistoryValid = true;
        scene_.find(grandchild)->motionHistoryValid = true;
        cpuPreviewRestartRequested_ = false;
        editorSession_.request(EditorCommand{EditorCommandType::SetEntityParent, child, first});
        processEditorCommands();
        check(scene_.find(child)->parent == first
              && !scene_.find(child)->motionHistoryValid
              && !scene_.find(grandchild)->motionHistoryValid
              && cpuPreviewRestartRequested_,
              "reparent command must invalidate the subtree and CPU preview");
        editorSession_.request(EditorCommand{EditorCommandType::SetEntityParent, first, grandchild});
        processEditorCommands();
        check(scene_.find(first)->parent == invalidSceneEntityId,
              "reparent command must reject hierarchy cycles");
        scene_.destroyEntity(grandchild);
        scene_.destroyEntity(child);

        const bool originalGroundReceiver = showGroundPlane_;
        const glm::vec3 originalGroundColor = groundColor_;
        const float originalGroundOffset = groundOffset_;
        const bool originalComparisonObject = showComparisonObject_;
        const glm::vec3 originalBaseColor = rendererSettings_.baseColor;
        const float originalShininess = rendererSettings_.shininess;
        const glm::vec3 originalLightDirection = rendererSettings_.lightDirection;
        const float originalAmbientStrength = rendererSettings_.ambientStrength;
        const float originalDiffuseStrength = rendererSettings_.diffuseStrength;
        const float originalSpecularStrength = rendererSettings_.specularStrength;

        EditorCommand stageCommand{EditorCommandType::SetStageSettings};
        stageCommand.stage.groundReceiver = false;
        stageCommand.stage.groundColor = {0.1f, 0.2f, 0.3f};
        stageCommand.stage.groundOffset = -1.25f;
        stageCommand.stage.comparisonObject = true;
        editorSession_.request(std::move(stageCommand));
        EditorCommand materialCommand{EditorCommandType::SetMaterialSettings};
        materialCommand.material.baseColor = {0.7f, 0.6f, 0.5f};
        materialCommand.material.shininess = 96.0f;
        editorSession_.request(std::move(materialCommand));
        EditorCommand lightCommand{EditorCommandType::SetDirectionalLightSettings};
        lightCommand.directionalLight.direction = {-0.25f, -0.9f, 0.1f};
        lightCommand.directionalLight.ambientStrength = 0.12f;
        lightCommand.directionalLight.diffuseStrength = 1.4f;
        lightCommand.directionalLight.specularStrength = 0.65f;
        editorSession_.request(std::move(lightCommand));
        processEditorCommands();
        check(!showGroundPlane_ && showComparisonObject_
              && std::abs(groundColor_.y - 0.2f) < 1.0e-6f
              && std::abs(groundOffset_ + 1.25f) < 1.0e-6f,
              "Renderer Stage command did not update the scene stage");
        check(std::abs(rendererSettings_.baseColor.x - 0.7f) < 1.0e-6f
              && std::abs(rendererSettings_.shininess - 96.0f) < 1.0e-6f,
              "Renderer Material command did not update renderer settings");
        check(std::abs(rendererSettings_.lightDirection.y + 0.9f) < 1.0e-6f
              && std::abs(rendererSettings_.ambientStrength - 0.12f) < 1.0e-6f
              && std::abs(rendererSettings_.diffuseStrength - 1.4f) < 1.0e-6f
              && std::abs(rendererSettings_.specularStrength - 0.65f) < 1.0e-6f,
              "Renderer Directional light command did not update renderer settings");

        EditorCommand restoreStage{EditorCommandType::SetStageSettings};
        restoreStage.stage.groundReceiver = originalGroundReceiver;
        restoreStage.stage.groundColor = {
            originalGroundColor.x, originalGroundColor.y, originalGroundColor.z
        };
        restoreStage.stage.groundOffset = originalGroundOffset;
        restoreStage.stage.comparisonObject = originalComparisonObject;
        editorSession_.request(std::move(restoreStage));
        EditorCommand restoreMaterial{EditorCommandType::SetMaterialSettings};
        restoreMaterial.material.baseColor = {
            originalBaseColor.x, originalBaseColor.y, originalBaseColor.z
        };
        restoreMaterial.material.shininess = originalShininess;
        editorSession_.request(std::move(restoreMaterial));
        EditorCommand restoreLight{EditorCommandType::SetDirectionalLightSettings};
        restoreLight.directionalLight.direction = {
            originalLightDirection.x, originalLightDirection.y, originalLightDirection.z
        };
        restoreLight.directionalLight.ambientStrength = originalAmbientStrength;
        restoreLight.directionalLight.diffuseStrength = originalDiffuseStrength;
        restoreLight.directionalLight.specularStrength = originalSpecularStrength;
        editorSession_.request(std::move(restoreLight));
        processEditorCommands();
        check(showGroundPlane_ == originalGroundReceiver
              && showComparisonObject_ == originalComparisonObject
              && std::abs(rendererSettings_.shininess - originalShininess) < 1.0e-6f
              && std::abs(rendererSettings_.diffuseStrength - originalDiffuseStrength) < 1.0e-6f,
              "Renderer domain command restore did not restore editor state");

        // A2b2b renderer domains. Each remaining Renderer Inspector group must reach
        // renderer state only through its domain snapshot command, and an invalid
        // payload must be rejected without touching the live settings.
        const EditorPbrEnvironmentSettingsPayload originalPbr =
            EditorDomain::capturePbrEnvironmentSettings(rendererSettings_);
        const EditorWaterSettingsPayload originalWater =
            EditorDomain::captureWaterSettings(rendererSettings_);
        const EditorShadingSettingsPayload originalShading =
            EditorDomain::captureShadingSettings(rendererSettings_);
        const EditorPostProcessingSettingsPayload originalPost =
            EditorDomain::capturePostProcessingSettings(rendererSettings_);
        const EditorRasterizationSettingsPayload originalRaster =
            EditorDomain::captureRasterizationSettings(rendererSettings_);
        const EditorCameraSettingsPayload originalCamera =
            EditorDomain::captureCameraSettings(camera_);
        const EditorRuntimeSettingsPayload originalRuntime =
            EditorDomain::captureRuntimeSettings(vsync_, rendererSettings_);
        const EditorGlassSettingsPayload originalGlass =
            EditorDomain::captureGlassSettings(rendererSettings_);
        const EditorCausticsSettingsPayload originalCaustics =
            EditorDomain::captureCausticsSettings(rendererSettings_);
        const EditorInstanceSettingsPayload originalInstance =
            EditorDomain::captureInstanceSettings(rendererSettings_);

        EditorPbrEnvironmentSettingsPayload pbr = originalPbr;
        pbr.pbrEnabled = !pbr.pbrEnabled;
        pbr.skyboxEnabled = !pbr.skyboxEnabled;
        pbr.environmentIntensity = 1.35f;
        pbr.shadowCascadeCount = 4;
        pbr.shadowCascadeSplitLambda = 0.35f;
        pbr.shadowCascadeDebugView = true;
        EditorCommand pbrCommand{EditorCommandType::SetPbrEnvironmentSettings};
        pbrCommand.pbrEnvironment = pbr;
        editorSession_.request(std::move(pbrCommand));

        EditorWaterSettingsPayload waterSettings = originalWater;
        waterSettings.enabled = true;
        waterSettings.preset = static_cast<int>(WaterPreset::Storm);
        waterSettings.quality = static_cast<int>(WaterQuality::Low);
        waterSettings.level = -0.6f;
        waterSettings.amplitude = 0.4f;
        EditorCommand waterCommand{EditorCommandType::SetWaterSettings};
        waterCommand.water = waterSettings;
        editorSession_.request(std::move(waterCommand));

        EditorShadingSettingsPayload shading = originalShading;
        shading.shadingMode = static_cast<int>(ShadingMode::Stylized);
        shading.renderPath = static_cast<int>(RenderPath::Deferred);
        shading.gBufferDebugView = static_cast<int>(GBufferDebugView::Albedo);
        shading.stylizedBandCount = 7;
        shading.stylizedRimIntensity = 1.75f;
        shading.stylizedDitherEnabled = true;
        shading.stylizedShadowTint = {0.11f, 0.22f, 0.33f};
        EditorCommand shadingCommand{EditorCommandType::SetShadingSettings};
        shadingCommand.shading = shading;
        editorSession_.request(std::move(shadingCommand));

        EditorPostProcessingSettingsPayload post = originalPost;
        post.ssaoEnabled = true;
        post.ssaoStrength = 2.25f;
        post.temporalAaEnabled = true;
        post.temporalHistoryWeight = 0.6f;
        post.exposure = 1.45f;
        EditorCommand postCommand{EditorCommandType::SetPostProcessingSettings};
        postCommand.postProcessing = post;
        editorSession_.request(std::move(postCommand));

        EditorRasterizationSettingsPayload raster = originalRaster;
        raster.wireframe = true;
        raster.cullBackFaces = true;
        raster.normalMapping = false;
        raster.msaaSamples = originalRaster.msaaSamples == 4 ? 1 : 4;
        raster.backgroundColor = {0.03f, 0.04f, 0.05f};
        EditorCommand rasterCommand{EditorCommandType::SetRasterizationSettings};
        rasterCommand.rasterization = raster;
        editorSession_.request(std::move(rasterCommand));

        EditorCameraSettingsPayload cameraSettings = originalCamera;
        cameraSettings.fieldOfViewDegrees = 61.0f;
        // Frame camera rebuilds the whole orbit pose (including the field of view), so
        // it is requested before the explicit camera settings command that follows it.
        camera_.setOrbitPose(glm::vec3(1.5f, 2.5f, -3.5f), 12.0f, 8.0f, 21.0f, 33.0f);
        EditorCommand frameCommand{EditorCommandType::FrameCamera};
        frameCommand.value = static_cast<std::uint64_t>(EditorCameraFrameTarget::Model);
        editorSession_.request(std::move(frameCommand));
        EditorCommand cameraCommand{EditorCommandType::SetCameraSettings};
        cameraCommand.camera = cameraSettings;
        editorSession_.request(std::move(cameraCommand));

        EditorRuntimeSettingsPayload runtime = originalRuntime;
        runtime.vsync = !runtime.vsync;
        runtime.shaderHotReloadEnabled = false;
        EditorCommand runtimeCommand{EditorCommandType::SetRuntimeSettings};
        runtimeCommand.runtime = runtime;
        editorSession_.request(std::move(runtimeCommand));

        EditorGlassSettingsPayload glassSettings = originalGlass;
        glassSettings.transmissionEnabled = false;
        glassSettings.refractionSteps = 20;
        glassSettings.volumeGlassAttenuationColor = {0.25f, 0.45f, 0.65f};
        glassSettings.dispersionStrength = 1.25f;
        glassSettings.glassDebugView = static_cast<int>(GlassDebugView::Thickness);
        EditorCommand glassCommand{EditorCommandType::SetGlassSettings};
        glassCommand.glass = glassSettings;
        editorSession_.request(std::move(glassCommand));

        EditorCausticsSettingsPayload caustics = originalCaustics;
        caustics.causticsEnabled = true;
        caustics.causticsMode = static_cast<int>(CausticsMode::Projector);
        caustics.causticsStrength = 3.25f;
        caustics.causticsDirection = {0.25f, 0.0f, -0.5f};
        caustics.causticsAnimated = true;
        EditorCommand causticsCommand{EditorCommandType::SetCausticsSettings};
        causticsCommand.caustics = caustics;
        editorSession_.request(std::move(causticsCommand));

        EditorInstanceSettingsPayload instance = originalInstance;
        instance.instanceOptimizationEnabled = true;
        instance.frustumCullingEnabled = false;
        EditorCommand instanceCommand{EditorCommandType::SetInstanceSettings};
        instanceCommand.instance = instance;
        editorSession_.request(std::move(instanceCommand));

        processEditorCommands();
        check(rendererSettings_.pbrEnabled == pbr.pbrEnabled
              && rendererSettings_.skyboxEnabled == pbr.skyboxEnabled
              && std::abs(rendererSettings_.environmentIntensity - 1.35f) < 1.0e-6f
              && rendererSettings_.shadowCascadeCount == 4
              && std::abs(rendererSettings_.shadowCascadeSplitLambda - 0.35f) < 1.0e-6f
              && rendererSettings_.shadowCascadeDebugView,
              "Renderer PBR environment command did not update renderer settings");
        check(rendererSettings_.water.enabled
              && rendererSettings_.water.preset == WaterPreset::Storm
              && rendererSettings_.water.quality == WaterQuality::Low
              && std::abs(rendererSettings_.water.level + 0.6f) < 1.0e-6f
              && std::abs(rendererSettings_.water.amplitude - 0.4f) < 1.0e-6f,
              "Water command did not update renderer settings");
        check(rendererSettings_.shadingMode == ShadingMode::Stylized
              && rendererSettings_.renderPath == RenderPath::Deferred
              && rendererSettings_.gBufferDebugView == GBufferDebugView::Albedo
              && rendererSettings_.stylizedBandCount == 7
              && std::abs(rendererSettings_.stylizedRimIntensity - 1.75f) < 1.0e-6f
              && rendererSettings_.stylizedDitherEnabled
              && std::abs(rendererSettings_.stylizedShadowTint.z - 0.33f) < 1.0e-6f,
              "Renderer shading command did not update renderer settings");
        check(rendererSettings_.ssaoEnabled
              && std::abs(rendererSettings_.ssaoStrength - 2.25f) < 1.0e-6f
              && rendererSettings_.temporalAaEnabled
              && std::abs(rendererSettings_.temporalHistoryWeight - 0.6f) < 1.0e-6f
              && std::abs(rendererSettings_.exposure - 1.45f) < 1.0e-6f,
              "Renderer post-processing command did not update renderer settings");
        check(rendererSettings_.wireframe && rendererSettings_.cullBackFaces
              && !rendererSettings_.normalMapping
              && rendererSettings_.msaaSamples == raster.msaaSamples
              && std::abs(rendererSettings_.backgroundColor.y - 0.04f) < 1.0e-6f,
              "Renderer rasterization command did not update renderer settings");
        check(std::abs(camera_.fieldOfView() - 61.0f) < 1.0e-4f,
              "Renderer camera command did not update the camera");
        check(std::abs(camera_.orbitState().distance - 3.2f) < 1.0e-4f
              && std::abs(camera_.orbitState().target.x - modelPosition_.x) < 1.0e-4f,
              "Renderer frame camera command did not reset the orbit pose");
        check(vsync_ == runtime.vsync && !rendererSettings_.shaderHotReloadEnabled,
              "Renderer runtime command did not update window settings");
        check(!rendererSettings_.transmissionEnabled
              && rendererSettings_.refractionSteps == 20
              && std::abs(rendererSettings_.volumeGlassAttenuationColor.y - 0.45f) < 1.0e-6f
              && std::abs(rendererSettings_.dispersionStrength - 1.25f) < 1.0e-6f
              && rendererSettings_.glassDebugView == GlassDebugView::Thickness,
              "Renderer glass command did not update renderer settings");
        check(rendererSettings_.causticsEnabled
              && rendererSettings_.causticsMode == CausticsMode::Projector
              && std::abs(rendererSettings_.causticsStrength - 3.25f) < 1.0e-6f
              && std::abs(rendererSettings_.causticsDirection.z + 0.5f) < 1.0e-6f
              && rendererSettings_.causticsAnimated,
              "Renderer caustics command did not update renderer settings");
        check(rendererSettings_.instanceOptimizationEnabled
              && !rendererSettings_.frustumCullingEnabled,
              "Renderer instance command did not update renderer settings");

        EditorCommand outOfRangeExposure{EditorCommandType::SetPostProcessingSettings};
        outOfRangeExposure.postProcessing =
            EditorDomain::capturePostProcessingSettings(rendererSettings_);
        outOfRangeExposure.postProcessing.exposure = 12.0f;
        editorSession_.request(std::move(outOfRangeExposure));
        EditorCommand outOfRangeFieldOfView{EditorCommandType::SetCameraSettings};
        outOfRangeFieldOfView.camera.fieldOfViewDegrees = 140.0f;
        editorSession_.request(std::move(outOfRangeFieldOfView));
        EditorCommand outOfRangeRefraction{EditorCommandType::SetGlassSettings};
        outOfRangeRefraction.glass = EditorDomain::captureGlassSettings(rendererSettings_);
        outOfRangeRefraction.glass.refractionSteps = 64;
        editorSession_.request(std::move(outOfRangeRefraction));
        EditorCommand unknownMsaa{EditorCommandType::SetRasterizationSettings};
        unknownMsaa.rasterization = EditorDomain::captureRasterizationSettings(rendererSettings_);
        unknownMsaa.rasterization.msaaSamples = 8;
        editorSession_.request(std::move(unknownMsaa));
        EditorCommand invalidWater{EditorCommandType::SetWaterSettings};
        invalidWater.water = EditorDomain::captureWaterSettings(rendererSettings_);
        invalidWater.water.preset = 4;
        invalidWater.water.quality = static_cast<int>(WaterQuality::High);
        editorSession_.request(std::move(invalidWater));
        processEditorCommands();
        check(std::abs(rendererSettings_.exposure - 1.45f) < 1.0e-6f
              && std::abs(camera_.fieldOfView() - 61.0f) < 1.0e-4f
              && rendererSettings_.refractionSteps == 20
              && rendererSettings_.msaaSamples == raster.msaaSamples
              && rendererSettings_.water.preset == WaterPreset::Storm
              && rendererSettings_.water.quality == WaterQuality::Low,
              "out-of-range renderer payloads must be rejected without side effects");

        // P1-0C module preview: the Viewport follows a module-driven runtime scene while
        // the edit scene stays untouched, and the frame index is the only animation input.
        {
            const glm::vec3 editTranslation = scene_.find(first)->transform.translation;
            const float baselineRotationY = scene_.find(first)->transform.rotationDegrees.y;
            EditorCommand activate{EditorCommandType::SetActiveModule};
            activate.text = BuiltinModules::turntableId;
            editorSession_.request(std::move(activate));
            processEditorCommands();
            editorSession_.setFrameRange(0, 23);
            editorSession_.setFrame(0);
            updateModulePreview();
            check(modulePreviewEnabled(), "module preview must report the selected module");
            check(&viewportScene() != &scene_, "an active module must render the runtime scene");
            check(viewportScene().size() == scene_.size(),
                  "the runtime scene must mirror the edit scene");
            check(std::abs(viewportScene().find(first)->transform.translation.x
                           - editTranslation.x) < 1.0e-4f,
                  "frame 0 must reproduce the authored transform");
            editorSession_.setFrame(6);
            updateModulePreview();
            check(std::abs(viewportScene().find(first)->transform.rotationDegrees.y
                           - (baselineRotationY + 90.0f)) < 1.0e-3f,
                  "frame 6 must show a quarter turn of the turntable");
            check(std::abs(scene_.find(first)->transform.rotationDegrees.y - baselineRotationY)
                      < 1.0e-4f,
                  "the module must never write into the edit scene");
            editorSession_.setFrame(0);
            updateModulePreview();
            check(std::abs(viewportScene().find(first)->transform.rotationDegrees.y
                           - baselineRotationY) < 1.0e-4f,
                  "scrubbing back must rebuild the frame instead of keeping the last state");
            EditorCommand parameter{EditorCommandType::SetModuleParameter};
            parameter.text = "degreesPerFrame";
            parameter.moduleParameter.type = static_cast<int>(ModuleParameterType::Float);
            parameter.moduleParameter.number = 45.0f;
            editorSession_.request(std::move(parameter));
            processEditorCommands();
            editorSession_.setFrame(1);
            updateModulePreview();
            check(std::abs(viewportScene().find(first)->transform.rotationDegrees.y
                           - (baselineRotationY + 45.0f)) < 1.0e-3f,
                  "a module parameter command must change the previewed frame");
            editorSession_.request(EditorCommand{EditorCommandType::SetActiveModule});
            processEditorCommands();
            updateModulePreview();
            check(!modulePreviewEnabled() && &viewportScene() == &scene_,
                  "clearing the module must return the Viewport to the edit scene");
            editorSession_.setFrame(0);
        }

        camera_.setOrbitPose(glm::vec3(0.5f, 0.5f, 0.5f), 30.0f, 20.0f, 12.0f, 40.0f);
        EditorCommand unknownFrameTarget{EditorCommandType::FrameCamera};
        unknownFrameTarget.value = 7U;
        editorSession_.request(std::move(unknownFrameTarget));
        processEditorCommands();
        check(std::abs(camera_.orbitState().distance - 12.0f) < 1.0e-4f
              && std::abs(camera_.fieldOfView() - 40.0f) < 1.0e-4f,
              "an unknown camera frame target must not move the camera");

        // A hand-edited or legacy .myscene is read without clamping, so capture has to
        // normalise a value its Inspector control cannot produce; otherwise the strict
        // entry point would make that whole domain permanently uneditable.
        rendererSettings_.exposure = 9.0f;
        rendererSettings_.stylizedBandCount = 32;
        EditorPostProcessingSettingsPayload healedPost =
            EditorDomain::capturePostProcessingSettings(rendererSettings_);
        EditorShadingSettingsPayload healedShading =
            EditorDomain::captureShadingSettings(rendererSettings_);
        check(std::abs(healedPost.exposure - 4.0f) < 1.0e-6f
              && healedShading.stylizedBandCount == 8,
              "domain capture must clamp values outside the Inspector range");
        EditorCommand healedPostCommand{EditorCommandType::SetPostProcessingSettings};
        healedPostCommand.postProcessing = healedPost;
        editorSession_.request(std::move(healedPostCommand));
        EditorCommand healedShadingCommand{EditorCommandType::SetShadingSettings};
        healedShadingCommand.shading = healedShading;
        editorSession_.request(std::move(healedShadingCommand));
        processEditorCommands();
        check(std::abs(rendererSettings_.exposure - 4.0f) < 1.0e-6f
              && rendererSettings_.stylizedBandCount == 8,
              "command entry point must accept a normalised domain payload");

        EditorCommand restorePbrCommand{EditorCommandType::SetPbrEnvironmentSettings};
        restorePbrCommand.pbrEnvironment = originalPbr;
        editorSession_.request(std::move(restorePbrCommand));
        EditorCommand restoreWaterCommand{EditorCommandType::SetWaterSettings};
        restoreWaterCommand.water = originalWater;
        editorSession_.request(std::move(restoreWaterCommand));
        EditorCommand restoreShadingCommand{EditorCommandType::SetShadingSettings};
        restoreShadingCommand.shading = originalShading;
        editorSession_.request(std::move(restoreShadingCommand));
        EditorCommand restorePostCommand{EditorCommandType::SetPostProcessingSettings};
        restorePostCommand.postProcessing = originalPost;
        editorSession_.request(std::move(restorePostCommand));
        EditorCommand restoreRasterCommand{EditorCommandType::SetRasterizationSettings};
        restoreRasterCommand.rasterization = originalRaster;
        editorSession_.request(std::move(restoreRasterCommand));
        EditorCommand restoreCameraCommand{EditorCommandType::SetCameraSettings};
        restoreCameraCommand.camera = originalCamera;
        editorSession_.request(std::move(restoreCameraCommand));
        EditorCommand restoreRuntimeCommand{EditorCommandType::SetRuntimeSettings};
        restoreRuntimeCommand.runtime = originalRuntime;
        editorSession_.request(std::move(restoreRuntimeCommand));
        EditorCommand restoreGlassCommand{EditorCommandType::SetGlassSettings};
        restoreGlassCommand.glass = originalGlass;
        editorSession_.request(std::move(restoreGlassCommand));
        EditorCommand restoreCausticsCommand{EditorCommandType::SetCausticsSettings};
        restoreCausticsCommand.caustics = originalCaustics;
        editorSession_.request(std::move(restoreCausticsCommand));
        EditorCommand restoreInstanceCommand{EditorCommandType::SetInstanceSettings};
        restoreInstanceCommand.instance = originalInstance;
        editorSession_.request(std::move(restoreInstanceCommand));
        processEditorCommands();
        check(rendererSettings_.pbrEnabled == originalPbr.pbrEnabled
              && rendererSettings_.shadingMode == static_cast<ShadingMode>(originalShading.shadingMode)
              && rendererSettings_.renderPath == static_cast<RenderPath>(originalShading.renderPath)
              && std::abs(rendererSettings_.stylizedRimIntensity - originalShading.stylizedRimIntensity) < 1.0e-6f
              && rendererSettings_.ssaoEnabled == originalPost.ssaoEnabled
              && std::abs(rendererSettings_.exposure - originalPost.exposure) < 1.0e-6f
              && rendererSettings_.wireframe == originalRaster.wireframe
              && rendererSettings_.msaaSamples == originalRaster.msaaSamples
              && std::abs(camera_.fieldOfView() - originalCamera.fieldOfViewDegrees) < 1.0e-4f
              && vsync_ == originalRuntime.vsync
              && rendererSettings_.transmissionEnabled == originalGlass.transmissionEnabled
              && rendererSettings_.glassDebugView == static_cast<GlassDebugView>(originalGlass.glassDebugView)
              && rendererSettings_.causticsEnabled == originalCaustics.causticsEnabled
              && rendererSettings_.instanceOptimizationEnabled == originalInstance.instanceOptimizationEnabled,
              "Renderer domain command restore did not restore editor state");

        camera_.reset();
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == first, "viewport picks visible geometry");
        std::vector<unsigned char> lastOutlinePixels;
        const auto outlinePixels = [&](SceneEntityId selected, int width, int height) {
            RendererSettings settings;
            settings.skyboxEnabled = false;
            settings.bloom = false;
            renderer_->render(scene_.buildRenderItems(), camera_, settings, width, height);
            renderer_->drawSelectionOutline(scene_.buildRenderItems(), camera_, selected, settings.cullBackFaces);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderer_->colorTexture());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::size_t orange = 0;
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                if (pixels[i] > 250 && pixels[i + 1] > 140 && pixels[i + 1] < 155 && pixels[i + 2] < 30) ++orange;
            }
            lastOutlinePixels = std::move(pixels);
            return orange;
        };
        check(outlinePixels(first, 400, 300) > 100, "selection produces orange outline");
        check(outlinePixels(0, 400, 300) == 0, "deselection removes outline");
        check(outlinePixels(first, 240, 180) > 50, "outline survives viewport resize");
        const auto isOrange = [](const std::vector<unsigned char>& pixels, std::size_t i) {
            return pixels[i] > 250 && pixels[i + 1] > 140 && pixels[i + 1] < 155 && pixels[i + 2] < 30;
        };
        const auto occluder = scene_.duplicateEntity(first);
        const auto originalTransform = scene_.find(first)->transform;
        for (int fixture = 0; fixture < 2; ++fixture) {
            for (float pitch : {20.0f, 55.0f}) {
                camera_.setOrbitPose(glm::vec3(0.0f), 35.0f, pitch, 5.0f, 45.0f);
                scene_.find(first)->transform = originalTransform;
                if (fixture == 0) {
                    scene_.find(first)->transform.scale = glm::vec3(4.0f, 0.01f, 4.0f);
                    scene_.find(first)->transform.translation.y = -0.72f;
                }
                scene_.find(occluder)->visible = false;
                scene_.updateWorldTransforms();
                check(outlinePixels(first, 400, 300) > 50, "unoccluded fixture has outline");
                const auto silhouettePixels = lastOutlinePixels;
                for (float offset : {0.0f, 0.65f}) {
                    auto* blocker = scene_.find(occluder);
                    blocker->visible = true;
                    blocker->transform = originalTransform;
                    blocker->transform.scale = glm::vec3(fixture == 0 ? 0.8f : 0.45f);
                    blocker->transform.translation = fixture == 0 ? glm::vec3(offset, 0.0f, 0.0f)
                        : camera_.position() * 0.22f + glm::vec3(offset, 0.0f, 0.0f);
                    scene_.updateWorldTransforms();
                    outlinePixels(first, 400, 300);
                    if (fixture == 0 && pitch == 55.0f && offset == 0.0f) {
                        if (const char* capture = std::getenv("MYRENDERER_OUTLINE_CAPTURE")) {
                            std::string error;
                            check(renderer_->saveScreenshot(std::filesystem::u8path(capture), error), "outline regression screenshot");
                        }
                    }
                    for (std::size_t i = 0; i < lastOutlinePixels.size(); i += 4)
                        check(!isOrange(lastOutlinePixels, i) || isOrange(silhouettePixels, i),
                            "occluder must not generate extra outline edges");
                }
            }
        }
        scene_.find(first)->transform = originalTransform;
        camera_.reset();
        scene_.find(occluder)->transform = originalTransform;
        scene_.find(occluder)->transform.translation = camera_.position() * 0.4f;
        scene_.find(occluder)->transform.scale = glm::vec3(1.1f);
        scene_.updateWorldTransforms();
        check(outlinePixels(first, 400, 300) == 0, "fully occluded selection has no outline");
        scene_.destroyEntity(occluder);
        scene_.updateWorldTransforms();
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 0, 0) == 0, "background clears selection");
        const auto second = scene_.duplicateEntity(first);
        scene_.find(second)->transform.translation = camera_.position() * 0.2f;
        scene_.updateWorldTransforms();
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == second, "nearest overlapping model selected");
        selectEntity(second); deleteSelectedEntity();
        check(!scene_.find(second) && selectedSceneEntity_ == 0, "delete updates selection");
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == first, "deleted model no longer picked");
        scene_.find(first)->visible = false;
        check(pickEntity(scene_.buildRenderItems(), 400, 300, 200, 150) == 0, "hidden models not picked");
        std::vector<RenderItem> stress{{importedModels_.front().get(), glm::mat4(1.0f)}};
        materializeStressEntities(stress);
        selectEntity(stress.front().entityId); deleteSelectedEntity();
        materializeStressEntities(stress);
        check(stress.empty(), "deleted stress instance stays deleted");
        loadModel(cube, true);
        newEmptyScene();
        pendingModelImport_->future.wait(); updateModelLoad();
        check(scene_.size() == 0 && importedModels_.empty(), "new scene discards in-flight import");
        const auto glass = sourceRoot_ / "assets/models/glass_volume_sphere.gltf";
        finishModelLoad(glass, findImporter(glass)->load(glass), true);
        check(scene_.size() == 1 && !glassVolumeDemoEnabled_ && !showComparisonObject_, "ordinary import does not activate fixture preset");
        newEmptyScene();
        check(scene_.size() == 0 && rendererSettings_.localLights.empty(), "new scene clears all content");
        check(glGetError() == GL_NO_ERROR, "picking leaves no OpenGL errors");
        std::cout << "Editor interaction validation: PASS\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Editor interaction validation: FAIL: " << error.what() << '\n';
        return false;
    }
}
