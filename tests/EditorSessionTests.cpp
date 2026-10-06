#include "app/EditorSession.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        EditorSession session;
        require(session.backend() == EditorRenderBackend::Raster, "default backend must be Raster");
        require(session.activity() == EditorActivity::Edit, "default activity must be Edit");

        session.requestBackend(EditorRenderBackend::CpuPathTraced);
        session.requestActivity(EditorActivity::Preview);
        session.requestPause(true);
        session.request(EditorCommand{EditorCommandType::DeleteEntity, 42U});
        EditorCommand submit{EditorCommandType::SubmitRenderJob};
        submit.text = "assets/renderjobs/01_cpu_reference.renderjob";
        session.request(std::move(submit));
        session.request(EditorCommand{EditorCommandType::MoveRenderJobUp, 17U});
        session.request(EditorCommand{EditorCommandType::RetryRenderJob, 23U});
        session.request(EditorCommand{EditorCommandType::RefreshAssetCatalog});
        EditorCommand openScene{EditorCommandType::OpenSceneAsset};
        openScene.text = "assets/scenes/fixtures/10_reference_pathtracer_pbr_hdri.myscene";
        session.request(std::move(openScene));
        EditorCommand importModel{EditorCommandType::ImportModelAsset};
        importModel.text = "assets/models/cube.obj";
        session.request(std::move(importModel));
        EditorCommand selectJob{EditorCommandType::SelectRenderJobAsset};
        selectJob.text = "assets/renderjobs/01_cpu_reference.renderjob";
        session.request(std::move(selectJob));
        EditorCommand transform{EditorCommandType::SetEntityTransform, 31U};
        transform.transform.translation = {1.0f, 2.0f, 3.0f};
        transform.transform.rotationDegrees = {10.0f, 20.0f, 30.0f};
        transform.transform.scale = {0.5f, 1.5f, 2.0f};
        session.request(std::move(transform));
        EditorCommand tint{EditorCommandType::SetEntityTint, 31U};
        tint.color = {0.2f, 0.4f, 0.8f};
        session.request(std::move(tint));
        session.request(EditorCommand{
            EditorCommandType::SetEntityCastsShadow, 31U, 0U, false
        });
        EditorCommand stage{EditorCommandType::SetStageSettings};
        stage.stage.groundReceiver = false;
        stage.stage.groundColor = {0.1f, 0.2f, 0.3f};
        stage.stage.groundOffset = -1.25f;
        stage.stage.comparisonObject = true;
        session.request(std::move(stage));
        EditorCommand material{EditorCommandType::SetMaterialSettings};
        material.material.baseColor = {0.7f, 0.6f, 0.5f};
        material.material.shininess = 96.0f;
        session.request(std::move(material));
        EditorCommand lighting{EditorCommandType::SetDirectionalLightSettings};
        lighting.directionalLight.direction = {-0.25f, -0.9f, 0.1f};
        lighting.directionalLight.ambientStrength = 0.12f;
        lighting.directionalLight.diffuseStrength = 1.4f;
        lighting.directionalLight.specularStrength = 0.65f;
        session.request(std::move(lighting));
        EditorCommand pbrEnvironment{EditorCommandType::SetPbrEnvironmentSettings};
        pbrEnvironment.pbrEnvironment.pbrEnabled = false;
        pbrEnvironment.pbrEnvironment.skyboxEnabled = false;
        pbrEnvironment.pbrEnvironment.coloredTransmissionShadowsEnabled = false;
        pbrEnvironment.pbrEnvironment.environmentIntensity = 0.75f;
        // Cascaded shadows live in the same domain as the other environment settings, so the payload
        // has to carry them: a value that silently dropped here would leave the Inspector unable to
        // change the cascade count at all.
        pbrEnvironment.pbrEnvironment.shadowCascadeCount = 4;
        pbrEnvironment.pbrEnvironment.shadowCascadeSplitLambda = 0.25f;
        pbrEnvironment.pbrEnvironment.shadowCascadeDebugView = true;
        session.request(std::move(pbrEnvironment));
        EditorCommand shading{EditorCommandType::SetShadingSettings};
        shading.shading.shadingMode = 1;
        shading.shading.renderPath = 1;
        shading.shading.gBufferDebugView = 3;
        shading.shading.stylizedBandCount = 5;
        shading.shading.stylizedRimIntensity = 1.25f;
        shading.shading.stylizedDitherEnabled = true;
        shading.shading.stylizedShadowTint = {0.1f, 0.2f, 0.3f};
        shading.shading.stylizedColorGradingLut = 2;
        shading.shading.stylizedDebugView = 4;
        session.request(std::move(shading));
        EditorCommand post{EditorCommandType::SetPostProcessingSettings};
        post.postProcessing.ssaoEnabled = true;
        post.postProcessing.ssaoStrength = 2.0f;
        post.postProcessing.temporalAaEnabled = true;
        post.postProcessing.temporalHistoryWeight = 0.75f;
        post.postProcessing.bloomIntensity = 0.35f;
        post.postProcessing.exposure = 1.6f;
        session.request(std::move(post));
        EditorCommand raster{EditorCommandType::SetRasterizationSettings};
        raster.rasterization.wireframe = true;
        raster.rasterization.showGrid = false;
        raster.rasterization.backgroundColor = {0.02f, 0.03f, 0.04f};
        raster.rasterization.msaaSamples = 1;
        session.request(std::move(raster));
        EditorCommand camera{EditorCommandType::SetCameraSettings};
        camera.camera.fieldOfViewDegrees = 62.0f;
        session.request(std::move(camera));
        EditorCommand runtime{EditorCommandType::SetRuntimeSettings};
        runtime.runtime.vsync = false;
        runtime.runtime.shaderHotReloadEnabled = false;
        session.request(std::move(runtime));
        EditorCommand glass{EditorCommandType::SetGlassSettings};
        glass.glass.transmissionEnabled = false;
        glass.glass.refractionSteps = 20;
        glass.glass.volumeGlassAttenuationColor = {0.2f, 0.4f, 0.6f};
        glass.glass.dispersionStrength = 1.5f;
        glass.glass.glassDebugView = 9;
        session.request(std::move(glass));
        EditorCommand caustics{EditorCommandType::SetCausticsSettings};
        caustics.caustics.causticsEnabled = true;
        caustics.caustics.causticsMode = 0;
        caustics.caustics.causticsStrength = 3.5f;
        caustics.caustics.causticsDirection = {0.25f, 0.0f, -0.5f};
        caustics.caustics.causticsAnimated = true;
        session.request(std::move(caustics));
        EditorCommand instance{EditorCommandType::SetInstanceSettings};
        instance.instance.instanceOptimizationEnabled = true;
        instance.instance.frustumCullingEnabled = false;
        instance.instance.lodSelectionEnabled = false;
        session.request(std::move(instance));
        EditorCommand frameCamera{EditorCommandType::FrameCamera};
        frameCamera.value = static_cast<std::uint64_t>(EditorCameraFrameTarget::Selection);
        session.request(std::move(frameCamera));
        EditorCommand sky{EditorCommandType::SetAtmosphereSettings};
        sky.atmosphere.enabled = true;
        sky.atmosphere.sunElevationDegrees = 6.5f;
        sky.atmosphere.sunAzimuthDegrees = 288.0f;
        sky.atmosphere.turbidity = 2.25f;
        sky.atmosphere.skyIntensity = 4.5f;
        sky.atmosphere.sunIntensity = 2.0f;
        sky.atmosphere.groundAlbedo = 0.4f;
        session.request(std::move(sky));
        const auto commands = session.takeCommands();
        require(commands.size() == 28U, "commands must preserve every UI request");
        require(commands[0].type == EditorCommandType::BackendChanged, "backend command order changed");
        require(commands[0].value == static_cast<std::uint64_t>(EditorRenderBackend::CpuPathTraced),
                "backend command must snapshot its payload");
        require(commands[1].value == static_cast<std::uint64_t>(EditorActivity::Preview),
                "activity command must snapshot its payload");
        require(commands[2].flag, "pause command must carry the requested state");
        require(commands[3].entity == 42U, "entity command payload was lost");
        require(commands[4].text == "assets/renderjobs/01_cpu_reference.renderjob",
                "Render Job path payload was lost");
        require(commands[5].type == EditorCommandType::MoveRenderJobUp
                && commands[5].entity == 17U,
                "Render Queue reorder command payload was lost");
        require(commands[6].type == EditorCommandType::RetryRenderJob
                && commands[6].entity == 23U,
                "Render Queue retry command payload was lost");
        require(commands[7].type == EditorCommandType::RefreshAssetCatalog,
                "asset refresh command order changed");
        require(commands[8].type == EditorCommandType::OpenSceneAsset
                && commands[8].text == "assets/scenes/fixtures/10_reference_pathtracer_pbr_hdri.myscene",
                "scene asset command payload was lost");
        require(commands[9].type == EditorCommandType::ImportModelAsset
                && commands[9].text == "assets/models/cube.obj",
                "model asset command payload was lost");
        require(commands[10].type == EditorCommandType::SelectRenderJobAsset
                && commands[10].text == "assets/renderjobs/01_cpu_reference.renderjob",
                "Render Job asset command payload was lost");
        require(commands[11].type == EditorCommandType::SetEntityTransform
                && commands[11].entity == 31U
                && commands[11].transform.translation.x == 1.0f
                && commands[11].transform.rotationDegrees.y == 20.0f
                && commands[11].transform.scale.z == 2.0f,
                "entity transform command payload was lost");
        require(commands[12].type == EditorCommandType::SetEntityTint
                && commands[12].entity == 31U
                && commands[12].color.x == 0.2f
                && commands[12].color.z == 0.8f,
                "entity tint command payload was lost");
        require(commands[13].type == EditorCommandType::SetEntityCastsShadow
                && commands[13].entity == 31U && !commands[13].flag,
                "entity shadow command payload was lost");
        require(commands[14].type == EditorCommandType::SetStageSettings
                && !commands[14].stage.groundReceiver
                && commands[14].stage.groundColor.y == 0.2f
                && commands[14].stage.groundOffset == -1.25f
                && commands[14].stage.comparisonObject,
                "Stage settings command payload was lost");
        require(commands[15].type == EditorCommandType::SetMaterialSettings
                && commands[15].material.baseColor.x == 0.7f
                && commands[15].material.shininess == 96.0f,
                "Material settings command payload was lost");
        require(commands[16].type == EditorCommandType::SetDirectionalLightSettings
                && commands[16].directionalLight.direction.y == -0.9f
                && commands[16].directionalLight.diffuseStrength == 1.4f
                && commands[16].directionalLight.specularStrength == 0.65f,
                "Directional Light settings command payload was lost");
        require(commands[17].type == EditorCommandType::SetPbrEnvironmentSettings
                && !commands[17].pbrEnvironment.pbrEnabled
                && !commands[17].pbrEnvironment.skyboxEnabled
                && !commands[17].pbrEnvironment.coloredTransmissionShadowsEnabled
                && commands[17].pbrEnvironment.environmentIntensity == 0.75f
                && commands[17].pbrEnvironment.shadowCascadeCount == 4
                && commands[17].pbrEnvironment.shadowCascadeSplitLambda == 0.25f
                && commands[17].pbrEnvironment.shadowCascadeDebugView,
                "PBR environment command payload was lost");
        require(commands[18].type == EditorCommandType::SetShadingSettings
                && commands[18].shading.shadingMode == 1
                && commands[18].shading.renderPath == 1
                && commands[18].shading.gBufferDebugView == 3
                && commands[18].shading.stylizedBandCount == 5
                && commands[18].shading.stylizedRimIntensity == 1.25f
                && commands[18].shading.stylizedDitherEnabled
                && commands[18].shading.stylizedShadowTint.z == 0.3f
                && commands[18].shading.stylizedColorGradingLut == 2
                && commands[18].shading.stylizedDebugView == 4,
                "Shading settings command payload was lost");
        require(commands[19].type == EditorCommandType::SetPostProcessingSettings
                && commands[19].postProcessing.ssaoEnabled
                && commands[19].postProcessing.ssaoStrength == 2.0f
                && commands[19].postProcessing.temporalAaEnabled
                && commands[19].postProcessing.temporalHistoryWeight == 0.75f
                && commands[19].postProcessing.bloomIntensity == 0.35f
                && commands[19].postProcessing.exposure == 1.6f,
                "Post-processing command payload was lost");
        require(commands[20].type == EditorCommandType::SetRasterizationSettings
                && commands[20].rasterization.wireframe
                && !commands[20].rasterization.showGrid
                && commands[20].rasterization.backgroundColor.y == 0.03f
                && commands[20].rasterization.msaaSamples == 1,
                "Rasterization command payload was lost");
        require(commands[21].type == EditorCommandType::SetCameraSettings
                && commands[21].camera.fieldOfViewDegrees == 62.0f,
                "Camera command payload was lost");
        require(commands[22].type == EditorCommandType::SetRuntimeSettings
                && !commands[22].runtime.vsync
                && !commands[22].runtime.shaderHotReloadEnabled,
                "Runtime command payload was lost");
        require(commands[23].type == EditorCommandType::SetGlassSettings
                && !commands[23].glass.transmissionEnabled
                && commands[23].glass.refractionSteps == 20
                && commands[23].glass.volumeGlassAttenuationColor.y == 0.4f
                && commands[23].glass.dispersionStrength == 1.5f
                && commands[23].glass.glassDebugView == 9,
                "Glass command payload was lost");
        require(commands[24].type == EditorCommandType::SetCausticsSettings
                && commands[24].caustics.causticsEnabled
                && commands[24].caustics.causticsMode == 0
                && commands[24].caustics.causticsStrength == 3.5f
                && commands[24].caustics.causticsDirection.z == -0.5f
                && commands[24].caustics.causticsAnimated,
                "Caustics command payload was lost");
        require(commands[25].type == EditorCommandType::SetInstanceSettings
                && commands[25].instance.instanceOptimizationEnabled
                && !commands[25].instance.frustumCullingEnabled
                && !commands[25].instance.lodSelectionEnabled,
                "Instance settings command payload was lost");
        require(commands[26].type == EditorCommandType::FrameCamera
                && commands[26].value
                    == static_cast<std::uint64_t>(EditorCameraFrameTarget::Selection),
                "Camera frame command payload was lost");
        require(commands[27].type == EditorCommandType::SetAtmosphereSettings
                && commands[27].atmosphere.enabled
                && commands[27].atmosphere.sunElevationDegrees == 6.5f
                && commands[27].atmosphere.sunAzimuthDegrees == 288.0f
                && commands[27].atmosphere.turbidity == 2.25f
                && commands[27].atmosphere.skyIntensity == 4.5f
                && commands[27].atmosphere.sunIntensity == 2.0f
                && commands[27].atmosphere.groundAlbedo == 0.4f,
                "Atmosphere settings command payload was lost");
        require(session.takeCommands().empty(), "taking commands must drain the queue");

        session.setFrameRange(10, 33);
        session.setFramesPerSecond(24);
        session.setFrame(100);
        require(session.frame() == 33, "timeline frame must clamp to the active range");
        require(std::abs(session.timeSeconds() - 1.375) < 1.0e-9, "timeline time must derive from frame/FPS");

        session.requestActivity(EditorActivity::Edit);
        require(!session.paused(), "returning to Edit must clear the runtime pause state");
        session.takeCommands();
        session.requestActivity(EditorActivity::Preview);
        session.takeCommands();
        session.request(EditorCommand{EditorCommandType::Step});
        require(session.paused(), "Step must pause shared playback");
        auto playbackCommands = session.takeCommands();
        require(playbackCommands.size() == 2
                && playbackCommands[0].type == EditorCommandType::PauseChanged
                && playbackCommands[1].type == EditorCommandType::Step,
                "Step must stop playback before advancing the frame");
        session.requestActivity(EditorActivity::Preview);
        require(!session.paused(), "Preview must resume an already selected paused activity");
        playbackCommands = session.takeCommands();
        require(playbackCommands.size() == 1
                && playbackCommands[0].type == EditorCommandType::PauseChanged
                && !playbackCommands[0].flag, "Preview must send the resume command");
        session.request(EditorCommand{EditorCommandType::Reset});
        require(session.paused(), "Reset must pause shared playback");
        playbackCommands = session.takeCommands();
        require(playbackCommands.size() == 2
                && playbackCommands[0].type == EditorCommandType::PauseChanged
                && playbackCommands[1].type == EditorCommandType::Reset,
                "Reset must stop playback before restoring the start frame");
        std::cout << "Editor session command and timeline tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor session tests failed: " << error.what() << '\n';
        return 1;
    }
}
