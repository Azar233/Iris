#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "scene/SceneDocument.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float left, float right) {
    return std::abs(left - right) < 1.0e-5f;
}

} // namespace

int main() {
    try {
        const std::filesystem::path acceptanceRoot =
            std::filesystem::temp_directory_path() / "MyRendererSceneDocumentAcceptance";
        const std::filesystem::path directory = acceptanceRoot / "scenes";
        std::filesystem::create_directories(directory);
        const std::filesystem::path scenePath = directory / "repeatable.myscene";
        const std::filesystem::path modelPath =
            acceptanceRoot / "assets" / "models" / "cube.obj";

        SceneDocument source;
        source.camera.target = glm::vec3(1.0f, 2.0f, -3.0f);
        source.camera.yawDegrees = -27.0f;
        source.camera.pitchDegrees = 18.0f;
        source.camera.distance = 7.5f;
        source.camera.fieldOfViewDegrees = 39.0f;
        source.renderer.renderPath = RenderPath::Deferred;
        source.renderer.enscapeCubeShaderEnabled = true;
        source.renderer.backgroundColor = glm::vec3(0.01f, 0.02f, 0.03f);
        source.renderer.ssaoEnabled = true;
        source.renderer.bloomIntensity = 0.37f;
        source.renderer.shadingMode = ShadingMode::Stylized;
        source.renderer.stylizedPreset = StylizedPreset::NightAurora;
        source.renderer.stylizedBandCount = 4;
        source.renderer.stylizedBandSoftness = 0.07f;
        source.renderer.stylizedSpecularSize = 0.24f;
        source.renderer.stylizedSpecularSoftness = 0.06f;
        source.renderer.stylizedRimWidth = 0.41f;
        source.renderer.stylizedRimSoftness = 0.11f;
        source.renderer.stylizedRimIntensity = 1.2f;
        source.renderer.stylizedShadowTint = glm::vec3(0.12f, 0.18f, 0.33f);
        source.renderer.stylizedRimColor = glm::vec3(0.2f, 0.7f, 1.0f);
        source.renderer.stylizedOutlineEnabled = false;
        source.renderer.stylizedOutlineWidth = 2.5f;
        source.renderer.stylizedOutlineDepthThreshold = 0.04f;
        source.renderer.stylizedOutlineNormalThreshold = 0.31f;
        source.renderer.stylizedOutlineColor = glm::vec3(0.05f, 0.08f, 0.12f);
        source.renderer.stylizedDitherEnabled = true;
        source.renderer.stylizedDitherStrength = 0.62f;
        source.renderer.stylizedHeightFogEnabled = true;
        source.renderer.stylizedHeightFogDensity = 0.21f;
        source.renderer.stylizedHeightFogBaseHeight = -0.65f;
        source.renderer.stylizedHeightFogFalloff = 1.4f;
        source.renderer.stylizedHeightFogColor = glm::vec3(0.18f, 0.28f, 0.46f);
        source.renderer.stylizedColorGradingEnabled = true;
        source.renderer.stylizedColorGradingLut = StylizedColorGradingLut::Painterly;
        source.renderer.stylizedColorGradingStrength = 0.73f;
        source.renderer.stylizedDebugView = StylizedDebugView::Dither;
        source.renderer.shadowCascadeCount = 4;
        source.renderer.shadowCascadeSplitLambda = 0.35f;
        source.renderer.shadowCascadeDebugView = true;
        source.renderer.water.enabled = true;
        source.renderer.environmentPreset = 2;
        source.renderer.water.preset = WaterPreset::Storm;
        source.renderer.water.quality = WaterQuality::Low;
        source.renderer.water.level = -0.8f;
        source.renderer.water.extent = 1800.0f;
        source.renderer.water.amplitude = 0.41f;
        source.renderer.water.roughness = 0.37f;
        source.renderer.water.reflectionStrength = 0.8f;
        source.renderer.water.rippleStrength = 0.65f;
        source.renderer.water.sunGlintStrength = 0.3f;
        source.renderer.water.deepWaterStrength = 0.7f;
        source.renderer.water.waveDiversity = 0.6f;
        source.renderer.water.nearMeshFocus = 0.8f;
        source.renderer.water.windDirection = {0.4f, -0.7f};
        source.camera.farPlane = 1500.0f;
        source.renderer.atmosphere.nightSkyEnabled = true;
        source.renderer.atmosphere.cloudHalfResolution = true;
        source.renderer.atmosphere.cloudTemporalEnabled = true;
        source.renderer.atmosphere.cloudShadowsEnabled = true;
        source.renderer.atmosphere.cloudGodRaysEnabled = true;
        source.renderer.atmosphere.cloudGodRaysStrength = 0.23f;
        source.renderer.atmosphere.cloudOfflineNoise = true;
        source.renderer.atmosphere.cloudDeterministic = true; // Preserve authored capture preference.
        source.renderer.atmosphere.moonIntensity = 1.5f;
        source.renderer.atmosphere.starIntensity = 0.7f;
        source.renderer.localLights.push_back(LocalLight{
            glm::vec3(2.0f, 3.0f, 4.0f),
            6.0f,
            glm::vec3(0.8f, 0.4f, 0.2f),
            12.0f,
            glm::vec3(0.0f, -1.0f, 0.0f),
            0.76f,
            LocalLightType::Spot
        });
        source.playback.animationEnabled = true;
        source.playback.animationTimeSeconds = 1.25f;
        source.playback.prismParameters.spectralSampleCount = 31;

        SceneDocumentEntity root;
        root.id = 7U;
        root.name = "Root model";
        root.modelResource = modelPath.generic_u8string();
        root.transform.translation = glm::vec3(2.0f, -1.0f, 0.5f);
        root.transform.rotationDegrees = glm::vec3(10.0f, 20.0f, 30.0f);
        root.transform.scale = glm::vec3(1.5f, 0.8f, 2.0f);
        root.transform.assetTransform[3][0] = -0.25f;
        root.tint = glm::vec3(0.2f, 0.7f, 0.9f);
        root.visible = false;
        root.castsShadow = false;
        source.entities.push_back(root);

        SceneDocumentEntity child;
        child.id = 11U;
        child.name = "Child instance";
        child.parent = root.id;
        child.modelResource = modelPath.generic_u8string();
        child.transform.translation = glm::vec3(0.0f, 3.0f, 0.0f);
        child.instanceCandidate = true;
        source.entities.push_back(child);

        std::string error;
        require(saveSceneDocument(scenePath, source, error), error.c_str());
        SceneDocument firstLoad;
        require(loadSceneDocument(scenePath, firstLoad, error), error.c_str());
        require(firstLoad.entities.size() == 2U, "first load entity count");
        require(firstLoad.entities[1].parent == 7U, "hierarchy survives first load");
        if (firstLoad.entities[0].modelResource != "../assets/models/cube.obj") {
            throw std::runtime_error(
                "model path is scene-relative (got '" + firstLoad.entities[0].modelResource + "')"
            );
        }
        require(!firstLoad.entities[0].visible && !firstLoad.entities[0].castsShadow, "visibility and shadow state survive");
        require(close(firstLoad.entities[0].transform.rotationDegrees.y, 20.0f), "transform survives first load");
        require(close(firstLoad.entities[0].tint.z, 0.9f), "tint survives first load");
        require(firstLoad.renderer.renderPath == RenderPath::Deferred && firstLoad.renderer.ssaoEnabled,
            "renderer mode survives first load");
        require(firstLoad.renderer.shadowCascadeCount == 4
                && close(firstLoad.renderer.shadowCascadeSplitLambda, 0.35f)
                && firstLoad.renderer.shadowCascadeDebugView,
                "cascade settings survive first load");
        require(firstLoad.renderer.water.enabled
                && firstLoad.renderer.environmentPreset == 2
                && firstLoad.renderer.water.preset == WaterPreset::Storm
                && firstLoad.renderer.water.quality == WaterQuality::Low
                && close(firstLoad.renderer.water.level, -0.8f)
                && close(firstLoad.renderer.water.extent, 1800.0f)
                && close(firstLoad.renderer.water.amplitude, 0.41f)
                && close(firstLoad.renderer.water.roughness, 0.37f)
                && close(firstLoad.renderer.water.reflectionStrength, 0.8f)
                && close(firstLoad.renderer.water.rippleStrength, 0.65f)
                && close(firstLoad.renderer.water.sunGlintStrength, 0.3f)
                && close(firstLoad.renderer.water.deepWaterStrength, 0.7f)
                && close(firstLoad.renderer.water.waveDiversity, 0.6f)
                && close(firstLoad.renderer.water.nearMeshFocus, 0.8f)
                && close(firstLoad.renderer.water.windDirection.x, 0.4f)
                && close(firstLoad.renderer.water.windDirection.y, -0.7f),
                "water settings survive first load");
        require(close(firstLoad.camera.farPlane, 1500.0f),
            "camera far plane survives first load");
        require(firstLoad.renderer.atmosphere.nightSkyEnabled
                && close(firstLoad.renderer.atmosphere.moonIntensity, 1.5f)
                && close(firstLoad.renderer.atmosphere.starIntensity, 0.7f),
                "night sky settings survive first load");
        require(firstLoad.renderer.atmosphere.cloudHalfResolution
                && firstLoad.renderer.atmosphere.cloudTemporalEnabled
                && firstLoad.renderer.atmosphere.cloudShadowsEnabled
                && firstLoad.renderer.atmosphere.cloudGodRaysEnabled
                && close(firstLoad.renderer.atmosphere.cloudGodRaysStrength, 0.23f)
                && firstLoad.renderer.atmosphere.cloudDeterministic
                && firstLoad.renderer.atmosphere.cloudOfflineNoise,
            "independent cloud reconstruction settings survive first load");
        require(firstLoad.renderer.shadingMode == ShadingMode::Stylized
                && firstLoad.renderer.stylizedPreset == StylizedPreset::NightAurora
                && firstLoad.renderer.stylizedBandCount == 4
                && close(firstLoad.renderer.stylizedBandSoftness, 0.07f)
                && close(firstLoad.renderer.stylizedSpecularSize, 0.24f)
                && close(firstLoad.renderer.stylizedSpecularSoftness, 0.06f)
                && close(firstLoad.renderer.stylizedRimWidth, 0.41f)
                && close(firstLoad.renderer.stylizedRimSoftness, 0.11f)
                && close(firstLoad.renderer.stylizedRimIntensity, 1.2f)
                && close(firstLoad.renderer.stylizedShadowTint.z, 0.33f)
                && close(firstLoad.renderer.stylizedRimColor.y, 0.7f)
                && !firstLoad.renderer.stylizedOutlineEnabled
                && close(firstLoad.renderer.stylizedOutlineWidth, 2.5f)
                && close(firstLoad.renderer.stylizedOutlineDepthThreshold, 0.04f)
                && close(firstLoad.renderer.stylizedOutlineNormalThreshold, 0.31f)
                && close(firstLoad.renderer.stylizedOutlineColor.z, 0.12f)
                && firstLoad.renderer.stylizedDitherEnabled
                && close(firstLoad.renderer.stylizedDitherStrength, 0.62f)
                && firstLoad.renderer.stylizedHeightFogEnabled
                && close(firstLoad.renderer.stylizedHeightFogDensity, 0.21f)
                && close(firstLoad.renderer.stylizedHeightFogBaseHeight, -0.65f)
                && close(firstLoad.renderer.stylizedHeightFogFalloff, 1.4f)
                && close(firstLoad.renderer.stylizedHeightFogColor.z, 0.46f)
                && firstLoad.renderer.stylizedColorGradingEnabled
                && firstLoad.renderer.stylizedColorGradingLut
                    == StylizedColorGradingLut::Painterly
                && close(firstLoad.renderer.stylizedColorGradingStrength, 0.73f)
                && firstLoad.renderer.stylizedDebugView == StylizedDebugView::Dither,
            "stylized renderer settings survive first load");
        require(firstLoad.renderer.localLights.size() == 1U
            && firstLoad.renderer.localLights.front().type == LocalLightType::Spot,
            "local environment light survives first load");
        require(close(firstLoad.camera.distance, 7.5f), "camera survives first load");
        require(firstLoad.renderer.enscapeCubeShaderEnabled,
            "GLSL study mode survives first load");

        require(saveSceneDocument(scenePath, firstLoad, error), error.c_str());
        SceneDocument secondLoad;
        require(loadSceneDocument(scenePath, secondLoad, error), error.c_str());
        require(secondLoad.entities.size() == firstLoad.entities.size(), "second load entity count is stable");
        require(secondLoad.entities[1].parent == firstLoad.entities[1].parent, "hierarchy is repeatable");
        require(secondLoad.entities[0].modelResource == firstLoad.entities[0].modelResource,
            "relative model path is repeatable");
        require(close(secondLoad.renderer.bloomIntensity, 0.37f), "renderer settings survive repeated load");
        require(secondLoad.renderer.enscapeCubeShaderEnabled,
            "GLSL study mode survives repeated load");
        require(secondLoad.renderer.shadingMode == ShadingMode::Stylized
                && secondLoad.renderer.stylizedPreset == StylizedPreset::NightAurora
                && secondLoad.renderer.stylizedBandCount == 4
                && close(secondLoad.renderer.stylizedSpecularSize, 0.24f)
                && close(secondLoad.renderer.stylizedRimIntensity, 1.2f)
                && close(secondLoad.renderer.stylizedShadowTint.z, 0.33f)
                && !secondLoad.renderer.stylizedOutlineEnabled
                && close(secondLoad.renderer.stylizedOutlineWidth, 2.5f)
                && secondLoad.renderer.stylizedDitherEnabled
                && close(secondLoad.renderer.stylizedDitherStrength, 0.62f)
                && secondLoad.renderer.stylizedHeightFogEnabled
                && close(secondLoad.renderer.stylizedHeightFogDensity, 0.21f)
                && close(secondLoad.renderer.stylizedHeightFogBaseHeight, -0.65f)
                && close(secondLoad.renderer.stylizedHeightFogFalloff, 1.4f)
                && secondLoad.renderer.stylizedColorGradingEnabled
                && secondLoad.renderer.stylizedColorGradingLut
                    == StylizedColorGradingLut::Painterly
                && close(secondLoad.renderer.stylizedColorGradingStrength, 0.73f)
                && secondLoad.renderer.stylizedDebugView == StylizedDebugView::Dither,
            "stylized renderer settings survive repeated load");
        require(close(secondLoad.playback.animationTimeSeconds, 1.25f), "playback state survives repeated load");
        require(resolveSceneResource(secondLoad.entities[0].modelResource, scenePath)
                == modelPath.lexically_normal(),
            "relative model path resolves against the scene file");

        auto invalidOffline=source;
        invalidOffline.renderer.atmosphere.cloudNoisePeriod=5;
        const auto invalidOfflinePath=directory/"invalid-offline.myscene";
        require(saveSceneDocument(invalidOfflinePath,invalidOffline,error),error.c_str());
        auto retained=firstLoad;
        require(!loadSceneDocument(invalidOfflinePath,retained,error),"offline asset period mismatch must reject");
        require(retained.renderer.atmosphere.cloudOfflineNoise
            && close(retained.renderer.atmosphere.cloudNoisePeriod,4)
            && retained.entities.size()==firstLoad.entities.size(),"failed offline load must preserve the previous scene");

        const std::filesystem::path examples =
            std::filesystem::path(MYRENDERER_SOURCE_DIR) / "assets" / "scenes";
        const std::filesystem::path fixtures = examples / "fixtures";
        std::size_t visibleSceneCount = 0U;
        std::size_t fixtureCount = 0U;
        bool foundPathTracingPbr = false;
        bool foundPathTracingLights = false;
        bool foundPathTracingVolume = false;
        const auto verifySceneDirectory = [&](const std::filesystem::path& directory,
                                              std::size_t& count) {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (!entry.is_regular_file()
                    || entry.path().extension() != myRendererSceneExtension) continue;
                SceneDocument example;
                require(loadSceneDocument(entry.path(), example, error), error.c_str());
                require(!example.entities.empty()
                    || (example.renderer.water.enabled && example.renderer.skyboxEnabled),
                    "bundled scene must contain entities or a renderable water/skybox environment");
                if (entry.path().filename() == "02_ocean_weather_hero.myscene") {
                    require(example.entities.empty()
                        && example.renderer.environmentPreset == 1
                        && close(example.renderer.water.deepWaterStrength, 1.0f)
                        && close(example.renderer.water.waveDiversity, 1.0f)
                        && close(example.renderer.water.nearMeshFocus, 1.0f),
                        "open-ocean hero must render without a seabed entity");
                }
                if (entry.path().filename() == "03_enscape_ocean_study.myscene") {
                    require(example.entities.empty()
                        && example.renderer.enscapeCubeShaderEnabled,
                        "ocean study must use the isolated four-pass GLSL renderer");
                }
                if (entry.path().filename() == "18_atmosphere_sky.myscene") {
                    require(!example.renderer.atmosphere.cloudHalfResolution
                        && !example.renderer.atmosphere.cloudTemporalEnabled
                        && !example.renderer.atmosphere.cloudShadowsEnabled
                        && !example.renderer.atmosphere.cloudGodRaysEnabled
                        && close(example.renderer.atmosphere.cloudGodRaysStrength, 0.08f)
                        && !example.renderer.atmosphere.cloudDeterministic
                        && !example.renderer.atmosphere.cloudOfflineNoise,
                        "older scenes must retain full-resolution deterministic cloud defaults");
                }
                for (const SceneDocumentEntity& entity : example.entities) {
                    if (entity.modelResource.empty()
                        || entity.modelResource.rfind("builtin:", 0U) == 0U) continue;
                    require(std::filesystem::is_regular_file(
                        resolveSceneResource(entity.modelResource, entry.path())
                    ), "bundled scene model resource must resolve");
                }
                foundPathTracingPbr = foundPathTracingPbr
                    || entry.path().filename() == "10_reference_pathtracer_pbr_hdri.myscene";
                foundPathTracingLights = foundPathTracingLights
                    || entry.path().filename() == "11_reference_pathtracer_lights.myscene";
                foundPathTracingVolume = foundPathTracingVolume
                    || entry.path().filename() == "12_reference_pathtracer_volume.myscene";
                ++count;
            }
        };
        verifySceneDirectory(examples, visibleSceneCount);
        verifySceneDirectory(fixtures, fixtureCount);
        require(visibleSceneCount == 3U,
            "the cloud lab and two ocean scenes should be user-visible");
        require(fixtureCount >= 22U, "all major feature scenes should remain as fixtures");
        require(foundPathTracingPbr && foundPathTracingLights && foundPathTracingVolume,
            "dedicated path-tracing scene fixtures should be bundled");

        std::cout << "Scene document repeat-load acceptance test passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Scene document acceptance test failed: " << exception.what() << '\n';
        return 1;
    }
}
