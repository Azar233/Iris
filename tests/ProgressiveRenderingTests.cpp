#include "pathtracer/ProgressiveRenderer.h"
#include "pathtracer/ReferenceComparison.h"
#include "pathtracer/MaterialBsdf.h"
#include "pathtracer/PbrBsdf.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include "asset/StbImageCompat.h"
using namespace pathtracer;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(glm::vec3 a, glm::vec3 b, float tolerance = 1e-5f) {
    require(glm::length(a - b) < tolerance, "RGB/vector mismatch");
}
glm::vec3 average(const RenderImage &image) {
    glm::vec3 value(0.0f);
    for (const auto &pixel : image.linearPixels())
        value += pixel;
    return value / static_cast<float>(image.sum.size());
}
SceneSnapshot plane(glm::vec3 albedo, glm::vec3 emission, glm::vec3 environment, bool reverse = false,
                    glm::vec3 tint = glm::vec3(1)) {
    auto model = std::make_shared<ModelData>();
    MaterialData m;
    m.baseColorFactor = glm::vec4(albedo, 1);
    m.emissiveFactor = emission;
    model->materials.push_back(m);
    MeshData mesh;
    for (auto p : {glm::vec3(-100, -100, -1), glm::vec3(100, -100, -1), glm::vec3(0, 100, -1)}) {
        Vertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = reverse ? std::vector<std::uint32_t>{0, 2, 1} : std::vector<std::uint32_t>{0, 1, 2};
    mesh.submeshes.push_back({"plane", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SceneSnapshotLighting lighting;
    lighting.environment.backgroundColor = environment;
    SceneSnapshotBuilder builder(SnapshotCamera{}, lighting);
    builder.addModel(model, 1, "plane", glm::mat4(1), tint);
    return builder.finish();
}
RenderImage render(SceneSnapshot scene, RenderSettings settings) {
    ProgressiveRenderer r(std::move(scene), settings);
    while (r.renderPass()) {
    }
    return r.image();
}
SceneSnapshot emissiveTriangleScene(SceneSnapshotLighting lighting = {}) {
    auto model = std::make_shared<ModelData>();
    MaterialData material;
    material.emissiveFactor = {4.0f, 2.0f, 1.0f};
    model->materials.push_back(material);
    MeshData mesh;
    for (const glm::vec3 position : {
             glm::vec3(-1, -1, 1), glm::vec3(0, 1, 1), glm::vec3(1, -1, 1)}) {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = {0, 0, -1};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2};
    mesh.submeshes.push_back({"Emitter", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SceneSnapshotBuilder builder(SnapshotCamera{}, std::move(lighting));
    builder.addModel(model, 1, "Emitter", glm::mat4(1));
    return builder.finish();
}
SnapshotEnvironment testEnvironment(float intensity = 1.0f) {
    SnapshotEnvironment environment;
    environment.intensity = intensity;
    environment.width = 16;
    environment.height = 8;
    environment.radiancePixels.assign(
        static_cast<std::size_t>(environment.width) * environment.height,
        glm::vec3(.015f, .02f, .03f)
    );
    for (std::uint32_t y = 3; y <= 4; ++y) {
        for (std::uint32_t x = 11; x <= 13; ++x) {
            environment.radiancePixels[static_cast<std::size_t>(y) * environment.width + x]
                = glm::vec3(24.0f, 18.0f, 10.0f);
        }
    }
    return environment;
}
SceneSnapshot environmentPlaneScene() {
    auto model = std::make_shared<ModelData>();
    MaterialData material;
    material.baseColorFactor = glm::vec4(.8f, .7f, .6f, 1.0f);
    material.roughnessFactor = 1.0f;
    model->materials.push_back(material);
    MeshData mesh;
    for (const glm::vec3 position : {
             glm::vec3(-100, -100, -1), glm::vec3(100, -100, -1), glm::vec3(0, 100, -1)}) {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = {0, 0, 1};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2};
    mesh.submeshes.push_back({"Environment receiver", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SceneSnapshotLighting lighting;
    lighting.environment = testEnvironment();
    SceneSnapshotBuilder builder(SnapshotCamera{}, lighting);
    builder.addModel(model, 1, "Environment receiver", glm::mat4(1));
    return builder.finish();
}
void environmentCameraVisibility() {
    SceneSnapshotLighting lighting;
    lighting.environment = testEnvironment(0.75f);
    lighting.environment.backgroundColor = {.1f, .2f, .3f};
    lighting.environment.visibleToCamera = false;
    SceneSnapshotBuilder builder(SnapshotCamera{}, lighting);
    const RenderImage hidden = render(builder.finish(), RenderSettings{1, 1, 1, 1, 9});
    near(hidden.linearPixels().front(), lighting.environment.backgroundColor, 1.0e-6f);
}
SceneSnapshot closedGlassSlabScene(float thickness) {
    auto model = std::make_shared<ModelData>();
    MaterialData material;
    material.baseColorFactor = glm::vec4(1);
    material.roughnessFactor = 0;
    material.transmissionFactor = 1;
    material.indexOfRefraction = 1;
    material.thicknessFactor = thickness;
    material.attenuationColor = {.25f, .5f, 1.0f};
    material.attenuationDistance = 1;
    model->materials.push_back(material);
    MeshData mesh;
    for (const glm::vec3 position : {
             glm::vec3(-100, -100, -1), glm::vec3(100, -100, -1), glm::vec3(0, 100, -1),
             glm::vec3(-100, -100, -2), glm::vec3(0, 100, -2), glm::vec3(100, -100, -2)}) {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = position.z > -1.5f ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes.push_back({"Closed glass slab", 0, 6, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SnapshotCamera camera;
    camera.aspectRatio = 1;
    camera.verticalFieldOfViewRadians = glm::radians(.01f);
    SceneSnapshotLighting lighting;
    lighting.environment.backgroundColor = glm::vec3(1);
    SceneSnapshotBuilder builder(camera, lighting);
    builder.addModel(model, 1, "Closed glass slab", glm::mat4(1));
    return builder.finish();
}
void cameraAndSampling() {
    SnapshotCamera c;
    c.aspectRatio = 2;
    c.verticalFieldOfViewRadians = glm::radians(90.0f);
    RenderSettings s{1, 1, 1, 1, 7};
    near(cameraRay(c, s, 0, 0, {.5f, .5f}).direction, {0, 0, -1});
    near(cameraRay(c, s, 0, 0, {0, 0}).direction, glm::normalize(glm::vec3(-2, 1, -1)));
    c.position = {2, 3, 4};
    c.view = glm::lookAt(c.position, c.position + glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    auto ray = cameraRay(c, s, 0, 0, {.5f, .5f});
    near(ray.origin, c.position);
    near(ray.direction, {1, 0, 0});
    Sampler a(7, 12, 9), b(7, 12, 9), different(7, 12, 10);
    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        float x = a.next();
        require(x >= 0 && x < 1 && x == b.next(), "RNG repeat/range");
        differs |= x != different.next();
    }
    require(differs, "Sample streams must differ");
    s.width = 0;
    bool rejected = false;
    try {
        cameraRay(c, s, 0, 0, {.5f, .5f});
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "Invalid settings accepted");
}
void energyAndDepth() {
    const glm::vec3 furnace = average(render(
        plane({1, 1, 1}, {0, 0, 0}, {1, 1, 1}, false, {.5f, .5f, .5f}), {4, 4, 2048, 2, 1, 0}));
    std::cout << "Tinted dielectric furnace: " << furnace.x << ", " << furnace.y << ", " << furnace.z << '\n';
    require(glm::all(glm::greaterThan(furnace, glm::vec3(0.15f))) &&
                glm::all(glm::lessThan(furnace, glm::vec3(0.19f))),
            "Dielectric white-furnace energy out of range");
    const auto scene = plane({.2f, .4f, .8f}, {2, 1, .5f}, {3, 2, 1});
    for (auto p : render(scene, {3, 3, 8, 1, 3}).linearPixels())
        near(p, {2, 1, .5f});
    const glm::vec3 reflected = average(render(scene, {4, 4, 2048, 2, 3, 0}));
    require(glm::all(glm::greaterThan(reflected, glm::vec3(2, 1, .5f))),
            "PBR reflection did not add environment radiance");
    for (auto p : render(plane({0, 0, 0}, {2, 1, .5f}, {0, 0, 0}, true), {2, 2, 1, 3, 1}).linearPixels())
        near(p, {0, 0, 0});
    for (auto p : render(plane({1, 1, 1}, {0, 0, 0}, {0, 0, 0}), {2, 2, 4, 8, 1}).linearPixels())
        near(p, {0, 0, 0});
}
void metallicRoughnessBsdf() {
    const glm::vec3 n(0, 0, 1), v(0, 0, 1), l(0, 0, 1);
    const auto smooth = evaluatePbrBsdf({{.8f, .8f, .8f}, 0, .15f}, n, v, l);
    const auto rough = evaluatePbrBsdf({{.8f, .8f, .8f}, 0, .8f}, n, v, l);
    require(smooth.x > rough.x, "Roughness did not broaden the GGX peak");
    const auto metal = evaluatePbrBsdf({{.9f, .08f, .02f}, 1, .3f}, n, v, l);
    require(metal.x > metal.y * 5 && metal.y > metal.z, "Metallic F0 did not inherit base color");
    require(glm::all(glm::equal(evaluatePbrBsdf({{1, 1, 1}, 0, .5f}, n, v, -l), glm::vec3(0))),
            "BSDF evaluated below the surface");

    Sampler random(20260914, 0, 0);
    glm::vec3 reflected(0.0f);
    constexpr int sampleCount = 100000;
    for (int i = 0; i < sampleCount; ++i) {
        const auto sample = samplePbrBsdf({{.8f, .6f, .3f}, .35f, .45f}, n, v, random.next(),
                                          {random.next(), random.next()});
        if (sample.valid) {
            require(sample.pdf > 0 && glm::all(glm::greaterThanEqual(sample.weight, glm::vec3(0))),
                    "Invalid PBR sample weight");
            reflected += sample.weight;
        }
    }
    reflected /= static_cast<float>(sampleCount);
    require(glm::all(glm::greaterThan(reflected, glm::vec3(.1f))) &&
                glm::all(glm::lessThan(reflected, glm::vec3(1.05f))),
            "PBR hemispherical reflectance is nonphysical");
}
void textureSamplingAndMaterialEvaluation() {
    TextureData source;
    source.width = 2;
    source.height = 2;
    source.rgbaPixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    const CpuTexture texture(source);
    require(texture.valid() && texture.width() == 2 && texture.height() == 2,
            "RGBA8 CPU texture did not decode");
    near(glm::vec3(texture.sampleRepeatBilinear({.25f, .25f})), {0, 0, 1});
    near(glm::vec3(texture.sampleRepeatBilinear({.75f, .25f})), {1, 1, 1});
    near(glm::vec3(texture.sampleRepeatBilinear({.25f, .75f})), {1, 0, 0});
    near(glm::vec3(texture.sampleRepeatBilinear({1.25f, -.25f})), {1, 0, 0});
    near(glm::vec3(texture.sampleRepeatBilinear({0, 0})), {.5f, .5f, .5f});

    TextureData srgb;
    srgb.width = srgb.height = 1;
    srgb.srgb = true;
    srgb.rgbaPixels = {128, 128, 128, 255};
    near(glm::vec3(CpuTexture(srgb).sampleRepeatBilinear({.5f, .5f})), glm::vec3(.2158605f), 1e-5f);
    srgb.width = 2;
    srgb.rgbaPixels = {0, 0, 0, 255, 255, 255, 255, 255};
    near(glm::vec3(CpuTexture(srgb).sampleRepeatBilinear({.5f, .5f})), glm::vec3(.5f), 1e-5f);

    TextureData encoded;
    encoded.encodedData = {
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
        0, 0, 0, 1, 0, 0, 0, 1, 8, 4, 0, 0, 0, 181, 28, 12, 2,
        0, 0, 0, 11, 73, 68, 65, 84, 120, 218, 99, 252, 255, 31, 0, 2,
        235, 1, 245, 105, 118, 117, 100, 0, 0, 0, 0, 73, 69, 78, 68,
        174, 66, 96, 130
    };
    const CpuTexture decoded(encoded);
    require(decoded.valid() && decoded.width() == 1 && decoded.height() == 1,
            "embedded PNG bytes did not decode");
    near(glm::vec3(decoded.sampleRepeatBilinear({0, 0})), glm::vec3(1));

    TextureData asciiPpm;
    const std::string ppm = "P3\n# CPU texture fixture\n1 1\n255\n32 64 128\n";
    asciiPpm.encodedData.assign(ppm.begin(), ppm.end());
    const CpuTexture decodedPpm(asciiPpm);
    require(decodedPpm.valid(), "ASCII PPM texture did not decode");
    near(glm::vec3(decodedPpm.sampleRepeatBilinear({.5f, .5f})), {32.0f / 255.0f, 64.0f / 255.0f,
                                                                  128.0f / 255.0f});

    auto model = std::make_shared<ModelData>();
    TextureData base = srgb;
    base.width = 1;
    base.rgbaPixels = {128, 255, 64, 128};
    TextureData packed;
    packed.width = packed.height = 1;
    packed.rgbaPixels = {0, 64, 128, 255};
    TextureData normal;
    normal.width = normal.height = 1;
    normal.rgbaPixels = {255, 128, 128, 255};
    TextureData thickness;
    thickness.width = thickness.height = 1;
    thickness.rgbaPixels = {0, 128, 0, 255};
    model->textures = {base, packed, normal, thickness};
    MaterialData material;
    material.baseColorFactor = {.5f, .5f, .5f, .5f};
    material.baseColorTextureIndex = 0;
    material.metallicRoughnessTextureIndex = 1;
    material.normalTextureIndex = 2;
    material.metallicFactor = .6f;
    material.roughnessFactor = .8f;
    material.transmissionFactor = .75f;
    material.indexOfRefraction = 1.45f;
    material.thicknessFactor = 2.0f;
    material.thicknessTextureIndex = 3;
    material.attenuationColor = {.25f, .5f, .75f};
    material.attenuationDistance = 3.0f;
    model->materials.push_back(material);
    MeshData mesh;
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    mesh.submeshes.push_back({"Textured", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SceneSnapshotBuilder builder(SnapshotCamera{});
    builder.addModel(model, 1, "Textured material", glm::mat4(1));
    const SceneSnapshot snapshot = builder.finish();
    const SceneTextures textures(snapshot);
    SurfaceInteraction hit;
    hit.assetIndex = 0;
    hit.texCoord = {.5f, .5f};
    hit.geometricNormal = hit.shadingNormal = {0, 0, 1};
    hit.tangent = {1, 0, 0, 1};
    const EvaluatedPbrMaterial evaluated = textures.evaluate(hit, material, glm::vec3(1));
    near(evaluated.surface.baseColor,
         {.5f * .2158605f, .5f, .5f * .0512695f}, 1e-5f);
    require(std::abs(evaluated.alpha - 128.0f / 255.0f * .5f) < 1e-5f,
            "Base-color alpha was not multiplied");
    require(std::abs(evaluated.surface.perceptualRoughness - .8f * 64.0f / 255.0f) < 1e-5f,
            "Packed roughness G channel was not applied");
    require(std::abs(evaluated.surface.metallic - .6f * 128.0f / 255.0f) < 1e-5f,
            "Packed metallic B channel was not applied");
    require(evaluated.shadingNormal.x > .999f && evaluated.shadingNormal.z > 0,
            "Tangent-space normal map did not perturb the shading normal");
    require(std::abs(evaluated.transmission - .75f) < 1e-5f
            && std::abs(evaluated.indexOfRefraction - 1.45f) < 1e-5f,
            "transmission or IOR was not evaluated");
    require(std::abs(evaluated.thickness - 2.0f * 128.0f / 255.0f) < 1e-5f,
            "thickness texture G channel was not applied");
    near(evaluated.attenuationColor, {.25f, .5f, .75f});

    hit.tangent.w = 0;
    near(textures.evaluate(hit, material, glm::vec3(1)).shadingNormal, hit.shadingNormal);

    material.baseColorTextureIndex = 99;
    material.metallicRoughnessTextureIndex = 99;
    material.normalTextureIndex = 99;
    const EvaluatedPbrMaterial fallback = textures.evaluate(hit, material, glm::vec3(1));
    near(fallback.surface.baseColor, glm::vec3(material.baseColorFactor));
    near(fallback.shadingNormal, hit.shadingNormal);
}
void dielectricTransmissionAndVolume() {
    require(std::abs(dielectricFresnel(1.0f, 1.0f, 1.5f) - .04f) < 1e-6f,
            "normal-incidence dielectric Fresnel mismatch");
    require(dielectricFresnel(.5f, 1.5f, 1.0f) == 1.0f,
            "inside-to-air critical angle did not produce total internal reflection");

    EvaluatedPbrMaterial glass;
    glass.surface = {{1, 1, 1}, 0, 0};
    glass.transmission = 1;
    glass.indexOfRefraction = 1.5f;
    const glm::vec3 normal(0, 0, 1);
    const glm::vec3 outgoing(0, 0, 1);
    const MaterialBsdfSample reflected = sampleMaterialBsdf(
        glass, normal, outgoing, true, .01f, {.5f, .5f}
    );
    require(reflected.valid && reflected.delta && !reflected.transmitted,
            "dielectric Fresnel reflection branch is invalid");
    near(reflected.direction, {0, 0, 1});
    near(reflected.weight, glm::vec3(1));

    const MaterialBsdfSample entered = sampleMaterialBsdf(
        glass, normal, outgoing, true, .5f, {.5f, .5f}
    );
    require(entered.valid && entered.delta && entered.transmitted,
            "air-to-dielectric refraction branch is invalid");
    near(entered.direction, {0, 0, -1});
    near(entered.weight, glm::vec3(1.0f / 2.25f));
    const MaterialBsdfSample exited = sampleMaterialBsdf(
        glass, normal, outgoing, false, .5f, {.5f, .5f}
    );
    require(exited.valid && exited.transmitted, "dielectric-to-air refraction branch is invalid");
    near(exited.weight, glm::vec3(2.25f));

    glass.transmission = .6f;
    require(std::abs(materialOpaqueProbability(glass) - .4f) < 1e-6f,
            "opaque/transmission mixture probability mismatch");
    near(beerLambertTransmittance({.25f, .5f, 1}, 2, 2), {.25f, .5f, 1});
    near(beerLambertTransmittance({.25f, .5f, 1}, 2, 1), {.5f, .70710678f, 1});

    RenderSettings settings{1, 1, 64, 3, 20260915, 0};
    const glm::vec3 absorbed = average(render(closedGlassSlabScene(1), settings));
    near(absorbed, {.25f, .5f, 1}, 2e-4f);
    near(average(render(closedGlassSlabScene(0), settings)), glm::vec3(1), 1e-5f);
}
void environmentImportanceSampling() {
    const SnapshotEnvironment source = testEnvironment(.75f);
    const EnvironmentLight environment(source);
    require(environment.importanceSampled() && environment.width() == 16 && environment.height() == 8,
            "linear HDR environment did not build an importance distribution");
    const EnvironmentSample sample = environment.sample({.5f, .5f});
    require(sample.valid && sample.pdf > 0 && glm::length(sample.radiance) > 0,
            "importance-sampled environment direction is invalid");
    require(std::abs(sample.pdf - environment.pdf(sample.direction)) < 1e-4f,
            "sampled and evaluated environment PDFs differ");

    Sampler random(20260915, 91, 7);
    std::uint32_t brightSamples = 0;
    constexpr std::uint32_t sampleCount = 10000;
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const EnvironmentSample drawn = environment.sample({random.next(), random.next()});
        require(drawn.valid, "environment distribution returned an invalid sample");
        if (drawn.radiance.r > 1.0f) ++brightSamples;
    }
    require(brightSamples > sampleCount * 9U / 10U,
            "luminance distribution did not concentrate samples on the bright HDR region");

    double normalizedPdf = 0.0;
    for (std::uint32_t index = 0; index < 200000U; ++index) {
        const float y = 1.0f - 2.0f * random.next();
        const float radius = std::sqrt(std::max(1.0f - y * y, 0.0f));
        const float phi = 6.28318530718f * random.next();
        const glm::vec3 direction(radius * std::cos(phi), y, radius * std::sin(phi));
        normalizedPdf += environment.pdf(direction) * 12.56637061436;
    }
    normalizedPdf /= 200000.0;
    require(std::abs(normalizedPdf - 1.0) < .02,
            "environment solid-angle PDF is not normalized");

    SnapshotEnvironment constant;
    constant.backgroundColor = {.2f, .3f, .4f};
    constant.intensity = 2;
    const EnvironmentLight background(constant);
    require(!background.importanceSampled(), "constant background should remain lookup-only");
    near(background.radiance({1, 0, 0}), {.4f, .6f, .8f});

    SceneSnapshotLighting lighting;
    lighting.environment = source;
    SceneSnapshotBuilder builder(SnapshotCamera{}, lighting);
    const SceneLights sceneLights(builder.finish(), {});
    const DirectLightSample direct = sceneLights.sample({0, 0, 0}, .25f, {.5f, .5f});
    require(direct.valid && !direct.delta && std::isinf(direct.distance),
            "environment was not registered as an infinite-area light");
    require(std::abs(direct.pdf - sceneLights.environmentPdf(direct.direction)) < 1e-4f,
            "scene-light selection probability is missing from environment PDF");

    const auto directory = std::filesystem::temp_directory_path() / "myrenderer-environment-test";
    const auto stem = directory / "source";
    RenderImage hdrImage;
    hdrImage.width = hdrImage.height = 2;
    hdrImage.completedSamples = 1;
    hdrImage.sum = {{8, 2, 1}, {1, 1, 1}, {.5f, .25f, .125f}, {2, 4, 8}};
    writeReferenceImage(hdrImage, stem);
    SnapshotEnvironment external;
    external.sourcePath = stem.string() + ".hdr";
    const EnvironmentLight decoded(external);
    require(decoded.importanceSampled() && decoded.width() == 2 && decoded.height() == 2,
            "external RGBE HDR environment did not decode");
    std::filesystem::remove(stem.string() + ".hdr");
    std::filesystem::remove(stem.string() + ".png");
}
void environmentDirectLightingAndVariance() {
    const SceneSnapshot scene = environmentPlaneScene();
    RenderSettings terminal{4, 4, 1, 1, 20260915};
    const glm::vec3 direct = average(render(scene, terminal));
    require(glm::length(direct) > .1f, "environment NEE did not contribute at terminal depth");
    terminal.nextEventEstimation = false;
    near(average(render(scene, terminal)), glm::vec3(0));

    RenderSettings referenceSettings{4, 4, 4096, 2, 20260915};
    const auto reference = render(scene, referenceSettings).linearPixels();
    RenderSettings lowSettings{4, 4, 16, 2, 20260915};
    const auto importanceSampled = render(scene, lowSettings).linearPixels();
    lowSettings.nextEventEstimation = false;
    const auto bsdfOnly = render(scene, lowSettings).linearPixels();
    double importanceError = 0.0;
    double bsdfError = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const glm::vec3 importanceDelta = importanceSampled[index] - reference[index];
        const glm::vec3 bsdfDelta = bsdfOnly[index] - reference[index];
        importanceError += glm::dot(importanceDelta, importanceDelta);
        bsdfError += glm::dot(bsdfDelta, bsdfDelta);
    }
    std::cout << "HDR environment MSE BSDF-only/importance at 16 SPP: "
              << bsdfError / (reference.size() * 3) << " / "
              << importanceError / (reference.size() * 3) << '\n';
    require(importanceError < bsdfError * .35,
            "HDR environment importance sampling did not materially reduce low-SPP error");
}
void directLightSampling() {
    const SceneSnapshot emitterScene = emissiveTriangleScene();
    const auto triangles = buildWorldTriangles(emitterScene);
    SceneLights areaLights(emitterScene, triangles);
    require(areaLights.size() == 1, "Emissive triangle was not registered as a light");
    const auto area = areaLights.sample({0, 0, 0}, .5f, {.25f, .5f});
    require(area.valid && !area.delta && area.distance > 0 && area.pdf > 0,
            "Emissive triangle sample is invalid");
    near(area.radiance, {4, 2, 1});
    SurfaceInteraction sampledHit;
    sampledHit.position = area.direction * area.distance;
    sampledHit.primitiveIndex = triangles.front().primitiveIndex;
    near(glm::vec3(areaLights.emissiveHitPdf({0, 0, 0}, sampledHit)), glm::vec3(area.pdf), 1e-4f);
    require(std::abs(powerHeuristic(2, 3) - 4.0f / 13.0f) < 1e-6f &&
                std::abs(powerHeuristic(2, 3) + powerHeuristic(3, 2) - 1.0f) < 1e-6f,
            "Power heuristic mismatch");

    SceneSnapshotLighting directionalLighting;
    directionalLighting.directional.direction = {0, 0, -1};
    directionalLighting.directional.radiance = {2, 3, 4};
    SceneSnapshotBuilder directionalBuilder(SnapshotCamera{}, directionalLighting);
    const auto directionalScene = directionalBuilder.finish();
    SceneLights directional(directionalScene, {});
    const auto sun = directional.sample({0, 0, 0}, .2f, {});
    require(sun.valid && sun.delta && std::isinf(sun.distance), "Directional light sample invalid");
    near(sun.direction, {0, 0, 1});
    near(sun.radiance, {2, 3, 4});

    SceneSnapshotLighting localLighting;
    localLighting.localLights.push_back({SnapshotLocalLightType::Point, {0, 0, 2}, {0, 0, -1},
                                         {10, 10, 10}, 4, .8f});
    SceneSnapshotBuilder localBuilder(SnapshotCamera{}, localLighting);
    const auto localScene = localBuilder.finish();
    SceneLights local(localScene, {});
    const auto point = local.sample({0, 0, 0}, .2f, {});
    near(point.radiance, glm::vec3(1.7578125f), 1e-5f);

    localLighting.localLights.front().type = SnapshotLocalLightType::Spot;
    SceneSnapshotBuilder spotBuilder(SnapshotCamera{}, localLighting);
    const auto spotScene = spotBuilder.finish();
    const auto spot = SceneLights(spotScene, {}).sample({0, 0, 0}, .2f, {});
    require(spot.valid, "Aligned spotlight rejected");
    near(spot.radiance, point.radiance);
}
void terminalDirectLighting() {
    auto model = std::make_shared<ModelData>();
    MaterialData material;
    material.baseColorFactor = glm::vec4(.8f, .6f, .3f, 1.0f);
    model->materials.push_back(material);
    MeshData mesh;
    for (const glm::vec3 position : {
             glm::vec3(-100, -100, -1), glm::vec3(100, -100, -1), glm::vec3(0, 100, -1)}) {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = {0, 0, 1};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2};
    mesh.submeshes.push_back({"Receiver", 0, 3, 0});
    model->meshes.push_back(mesh);
    model->rootNode.meshIndices = {0};
    SceneSnapshotLighting lighting;
    lighting.directional.direction = glm::normalize(glm::vec3(-1, 0, -1));
    lighting.directional.radiance = {2, 2, 2};
    SceneSnapshotBuilder builder(SnapshotCamera{}, lighting);
    builder.addModel(model, 1, "Receiver", glm::mat4(1));
    const auto scene = builder.finish();
    RenderSettings settings{4, 4, 1, 1, 9};
    const glm::vec3 visible = average(render(scene, settings));
    require(glm::length(visible) > .1f,
            "NEE did not evaluate direct light at terminal depth");

    auto blockedModel = std::make_shared<ModelData>(*model);
    MeshData blocker;
    for (const glm::vec3 position : {
             glm::vec3(.5f, -100, -1.5f), glm::vec3(.5f, 100, -1.5f),
             glm::vec3(.5f, 100, .5f), glm::vec3(.5f, -100, .5f)}) {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = {-1, 0, 0};
        blocker.vertices.push_back(vertex);
    }
    blocker.indices = {0, 1, 2, 0, 2, 3};
    blocker.submeshes.push_back({"Blocker", 0, 6, 0});
    blockedModel->rootNode.meshIndices.push_back(static_cast<std::uint32_t>(blockedModel->meshes.size()));
    blockedModel->meshes.push_back(std::move(blocker));
    SceneSnapshotBuilder blockedBuilder(SnapshotCamera{}, lighting);
    blockedBuilder.addModel(blockedModel, 1, "Blocked receiver", glm::mat4(1));
    require(glm::length(average(render(blockedBuilder.finish(), settings))) < glm::length(visible) * .01f,
            "Shadow ray did not reject occluded direct light");

    settings.nextEventEstimation = false;
    near(average(render(scene, settings)), glm::vec3(0));
}
void accumulationAndTasks() {
    auto scene = makeDiffuseAcceptanceScene();
    RenderSettings s{12, 12, 8, 5, 42};
    const auto expected = render(scene, s);
    auto noRoulette = s;
    noRoulette.russianRouletteDepth = 0;
    require(render(scene, noRoulette).sum != expected.sum, "Russian Roulette path was not exercised");
    ProgressiveRenderer r(scene, s);
    require(r.renderPass(), "First pass");
    auto saved = r.image();
    std::atomic<bool> stop{true};
    require(!r.renderPass(&stop), "Cancelled pass committed");
    require(saved.sum == r.image().sum, "Cancelled image changed");
    stop = false;
    while (r.renderPass(&stop)) {
    }
    require(r.image().sum == expected.sum, "Resume differs");
    require(render(scene, s).sum == expected.sum, "Render not deterministic");
    RenderTask task;
    const std::uint64_t firstTask = task.start(scene, s);
    task.wait();
    auto progress = task.progress();
    require(progress.taskId == firstTask && progress.status == RenderStatus::Completed
                && progress.image.sum == expected.sum,
            "Worker differs");
    require(progress.staging.rgba == makeDisplayRgba8BottomUp(expected, RenderOutput::Beauty),
            "Background staging differs from the synchronous renderer");
    auto longSettings = s;
    longSettings.samplesPerPixel = 1000000;
    task.start(scene, longSettings);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (task.progress().image.completedSamples == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    require(task.progress().image.completedSamples > 0, "No progressive publication");
    task.cancel();
    task.wait();
    progress = task.progress();
    require(progress.status == RenderStatus::Cancelled, "Cancellation status");
    auto prefix = s;
    prefix.samplesPerPixel = progress.image.completedSamples;
    require(progress.image.sum == render(scene, prefix).sum, "Cancelled prefix contains partial pass");
    const std::uint64_t staleTask = task.start(scene, longSettings);
    const std::uint64_t replacementTask = task.start(scene, s);
    require(replacementTask > staleTask, "Task generations did not advance");
    task.wait();
    progress = task.progress();
    require(progress.taskId == replacementTask && progress.image.sum == expected.sum,
            "Stale restart publication replaced the current task");
    s.maxDepth = 0;
    task.start(scene, s);
    task.wait();
    require(task.progress().status == RenderStatus::Failed && !task.progress().error.empty(),
            "Worker error not reported");
    {
        RenderTask scoped;
        scoped.start(scene, longSettings);
    } // Destructor must cancel/join.
}
void aovAccumulation() {
    const SceneSnapshot scene = environmentPlaneScene();
    RenderSettings settings{4, 4, 64, 2, 20260915};
    ProgressiveRenderer progressive(scene, settings);
    require(progressive.renderPass(), "AOV first pass failed");
    for (const float variance : progressive.image().variancePixels())
        require(variance == 0.0f, "Single-sample variance must be zero");
    while (progressive.renderPass()) {
    }
    const RenderImage &image = progressive.image();
    const auto beauty = image.linearPixels();
    const auto albedo = image.albedoPixels();
    const auto normal = image.normalPixels();
    const auto depth = image.depthPixels();
    const auto direct = image.directPixels();
    const auto indirect = image.indirectPixels();
    const auto sampleCount = image.sampleCountPixels();
    const auto variance = image.variancePixels();
    bool hasVariance = false;
    for (std::size_t index = 0; index < beauty.size(); ++index) {
        near(albedo[index], {.8f, .7f, .6f}, 1e-4f);
        near(normal[index], {0.0f, 0.0f, 1.0f}, 1e-4f);
        require(depth[index] > 0.0f && std::isfinite(depth[index]), "Invalid primary depth AOV");
        near(beauty[index], direct[index] + indirect[index], 1e-4f);
        require(sampleCount[index] == 64.0f, "Sample-count AOV mismatch");
        require(variance[index] >= 0.0f && std::isfinite(variance[index]), "Invalid variance AOV");
        hasVariance |= variance[index] > 0.0f;
    }
    require(hasVariance, "Stochastic scene produced no measurable variance");
}
void adaptiveSampling() {
    SceneSnapshotLighting constantLighting;
    constantLighting.environment.backgroundColor = {.2f, .3f, .4f};
    SceneSnapshotBuilder constantBuilder(SnapshotCamera{}, constantLighting);
    RenderSettings constantSettings{4U, 4U, 32U, 1U, 20260915U};
    constantSettings.adaptiveSampling = true;
    constantSettings.adaptiveMinimumSamples = 4U;
    constantSettings.adaptiveCheckInterval = 2U;
    constantSettings.adaptiveRelativeError = .01f;
    constantSettings.adaptiveAbsoluteError = .0001f;
    ProgressiveRenderer constantRenderer(constantBuilder.finish(), constantSettings);
    while (constantRenderer.renderPass()) {}
    require(constantRenderer.complete() && constantRenderer.image().completedSamples == 4U,
            "Zero-variance adaptive render did not stop at minimum SPP");
    require(std::all_of(constantRenderer.image().sampleCounts.begin(),
                        constantRenderer.image().sampleCounts.end(),
                        [](std::uint32_t count) { return count == 4U; }),
            "Adaptive sample counts did not stop together for a constant environment");
    require(constantRenderer.image().statistics.cameraSamples == 4U * 4U * 4U
                && constantRenderer.image().statistics.activePixels == 0U
                && constantRenderer.image().statistics.convergedPixels == 16U
                && constantRenderer.image().statistics.adaptiveChecks == 1U,
            "Adaptive convergence statistics mismatch");

    RenderTask task;
    SceneSnapshotBuilder taskBuilder(SnapshotCamera{}, constantLighting);
    task.start(taskBuilder.finish(), constantSettings);
    task.wait();
    require(task.progress().status == RenderStatus::Completed
                && task.progress().image.completedSamples == 4U,
            "Early adaptive completion was reported as cancellation");

    RenderSettings serialSettings{12U, 12U, 64U, 5U, 42U};
    serialSettings.workerCount = 1U;
    serialSettings.adaptiveSampling = true;
    serialSettings.adaptiveMinimumSamples = 8U;
    serialSettings.adaptiveCheckInterval = 8U;
    serialSettings.adaptiveRelativeError = .2f;
    serialSettings.adaptiveAbsoluteError = .01f;
    const RenderImage serial = render(makePbrAcceptanceScene(), serialSettings);
    RenderSettings parallelSettings = serialSettings;
    parallelSettings.workerCount = 4U;
    const RenderImage parallel = render(makePbrAcceptanceScene(), parallelSettings);
    require(serial.sum == parallel.sum && serial.directSum == parallel.directSum
                && serial.indirectSum == parallel.indirectSum
                && serial.luminanceSum == parallel.luminanceSum
                && serial.luminanceSquaredSum == parallel.luminanceSquaredSum
                && serial.sampleCounts == parallel.sampleCounts,
            "Adaptive sampling changed with worker scheduling");
    require(serial.statistics.cameraSamples == parallel.statistics.cameraSamples
                && serial.statistics.cameraSamples < 12U * 12U * 64U,
            "Adaptive sampling did not save deterministic camera samples");
    const auto counts = serial.sampleCountPixels();
    require(*std::min_element(counts.begin(), counts.end()) >= 8.0f
                && *std::max_element(counts.begin(), counts.end()) <= 64.0f,
            "Adaptive sample-count AOV left the configured range");
    const auto beauty = serial.linearPixels();
    const auto direct = serial.directPixels();
    const auto indirect = serial.indirectPixels();
    for (std::size_t index = 0U; index < beauty.size(); ++index)
        near(beauty[index], direct[index] + indirect[index], 1.0e-4f);
}
void tileSchedulingAndProfiles() {
    const auto tiles = makeRenderTiles(35, 19, 16);
    require(tiles.size() == 6, "Unexpected tile count for non-divisible image");
    require(tiles.front().xBegin == 0 && tiles.front().yBegin == 0
                && tiles.front().xEnd == 16 && tiles.front().yEnd == 16,
            "First tile bounds mismatch");
    require(tiles.back().xBegin == 32 && tiles.back().yBegin == 16
                && tiles.back().xEnd == 35 && tiles.back().yEnd == 19,
            "Clipped edge tile bounds mismatch");
    std::vector<unsigned int> coverage(35U * 19U, 0U);
    for (const RenderTile tile : tiles)
        for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
            for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
                ++coverage[static_cast<std::size_t>(y) * 35U + x];
    require(std::all_of(coverage.begin(), coverage.end(), [](unsigned int count) { return count == 1U; }),
            "Tiles did not cover every pixel exactly once");

    TileThreadPool reusablePool(2);
    bool propagated = false;
    try {
        reusablePool.execute(4, nullptr, [](std::size_t item) {
            if (item == 0U) throw std::runtime_error("expected worker failure");
        });
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    require(propagated, "Worker exception was not propagated to the render thread");
    std::atomic<unsigned int> executed{0U};
    require(reusablePool.execute(7, nullptr, [&executed](std::size_t) { ++executed; })
                && executed.load() == 7U,
            "Thread pool was not reusable after a worker exception");

    RenderSettings serialSettings{12, 12, 8, 5, 42};
    serialSettings.workerCount = 1;
    serialSettings.tileSize = 5;
    const RenderImage serial = render(makePbrAcceptanceScene(), serialSettings);
    RenderSettings parallelSettings = serialSettings;
    parallelSettings.workerCount = 4;
    const RenderImage parallel = render(makePbrAcceptanceScene(), parallelSettings);
    require(serial.sum == parallel.sum && serial.albedoSum == parallel.albedoSum
                && serial.normalSum == parallel.normalSum && serial.depthSum == parallel.depthSum
                && serial.primaryHitCount == parallel.primaryHitCount
                && serial.directSum == parallel.directSum && serial.indirectSum == parallel.indirectSum
                && serial.luminanceSum == parallel.luminanceSum
                && serial.luminanceSquaredSum == parallel.luminanceSquaredSum,
            "Parallel tile scheduling changed deterministic Beauty/AOV output");
    require(serial.statistics.workerCount == 1 && parallel.statistics.workerCount == 4,
            "Requested worker count was not applied");
    require(serial.statistics.completedTiles == 72 && parallel.statistics.completedTiles == 72,
            "Completed tile statistics mismatch");
    require(serial.statistics.pathRays == parallel.statistics.pathRays
                && serial.statistics.shadowRays == parallel.statistics.shadowRays
                && serial.statistics.bvhTraversal.boundsTests
                    == parallel.statistics.bvhTraversal.boundsTests
                && serial.statistics.bvhTraversal.triangleTests
                    == parallel.statistics.bvhTraversal.triangleTests,
            "Parallel traversal statistics are not deterministic");
    require(parallel.statistics.pathRays > 12U * 12U * 8U
                && parallel.statistics.shadowRays > 0U
                && parallel.statistics.bvhTraversal.boundsTests > parallel.statistics.pathRays
                && parallel.statistics.bvhTraversal.triangleTests > 0U
                && parallel.statistics.bvhBuild.nodeCount > 0U
                && parallel.statistics.renderMilliseconds > 0.0,
            "Render/BVH profile did not record useful work");
}
void twoLevelInstancing() {
    const auto scene = makeInstancingStressScene(6U);
    RenderSettings settings{18U, 12U, 2U, 3U, 20260915U};
    settings.workerCount = 2U;
    settings.accelerationStructure = AccelerationStructure::Automatic;
    ProgressiveRenderer automatic(scene, settings);
    require(automatic.image().statistics.accelerationStructure
                == AccelerationStructure::TwoLevelBvh,
            "Automatic acceleration selection ignored shared geometry");
    settings.accelerationStructure = AccelerationStructure::WorldBvh;
    const RenderImage world = render(scene, settings);
    settings.accelerationStructure = AccelerationStructure::TwoLevelBvh;
    const RenderImage twoLevel = render(scene, settings);

    double squaredError = 0.0;
    double energy = 0.0;
    for (std::size_t index = 0; index < world.sum.size(); ++index) {
        const glm::vec3 difference = world.sum[index] - twoLevel.sum[index];
        squaredError += glm::dot(difference, difference);
        energy += glm::dot(world.sum[index], world.sum[index]);
    }
    const double relativeRmse = std::sqrt(squaredError / std::max(energy, 1.0e-20));
    require(relativeRmse <= 1.0e-5, "BLAS/TLAS changed the fixed instancing scene");
    require(world.primaryHitCount == twoLevel.primaryHitCount,
            "BLAS/TLAS changed primary visibility");
    const auto& build = twoLevel.statistics.instancedBvhBuild;
    require(build.blasCount == 2U && build.instanceCount == 37U,
            "BLAS/TLAS did not preserve the fixed scene instance topology");
    require(build.uniquePrimitiveCount == 14U && build.expandedPrimitiveCount == 434U,
            "BLAS/TLAS primitive sharing statistics mismatch");
    require(build.uniquePrimitiveCount * 20U < build.expandedPrimitiveCount,
            "BLAS/TLAS did not materially share geometry");
    require(twoLevel.statistics.bvhTraversal.instanceTests > 0U,
            "TLAS traversal did not test any instances");
}
// Authored bytes exercise flat RGBE decoding without depending on our writer.
void flatHdrDecoding() {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    std::vector<unsigned char> bytes(header.begin(), header.end());
    const unsigned char pixels[] = {128,0,0,131, 0,128,0,129, 0,0,128,127, 0,0,0,0};
    bytes.insert(bytes.end(), std::begin(pixels), std::end(pixels));
    int width = 0, height = 0, components = 0;
    float* decoded = stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                          &width, &height, &components, 3);
    require(decoded && width == 2 && height == 2, "Authored flat HDR decode failed");
    const glm::vec3 expected[] = {{4,0,0}, {0,1,0}, {0,0,.25f}, {0,0,0}};
    bool correct = true;
    for (int pixel = 0; pixel < 4; ++pixel) {
        for (int channel = 0; channel < 3; ++channel)
            correct = correct && decoded[pixel*3+channel] == expected[pixel][channel];
    }
    stbi_image_free(decoded);
    require(correct, "Flat HDR second row/exponent decode mismatch");
}
void output() {
    RenderImage image;
    image.width = 2;
    image.height = 2;
    image.completedSamples = 1;
    image.sum = {{4, 0, 0}, {0, 1, 0}, {0, 0, .25f}, {0, 0, 0}};
    image.albedoSum = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
    image.normalSum.assign(4, glm::vec3(0, 0, 1));
    image.depthSum = {1, 2, 3, 4};
    image.primaryHitCount.assign(4, 1);
    image.directSum = image.sum;
    image.indirectSum.assign(4, glm::vec3(0));
    image.luminanceSum = {0.8504f, 0.7152f, 0.01805f, 0};
    image.luminanceSquaredSum = {
        image.luminanceSum[0] * image.luminanceSum[0],
        image.luminanceSum[1] * image.luminanceSum[1],
        image.luminanceSum[2] * image.luminanceSum[2],
        0
    };
    const auto directory = std::filesystem::temp_directory_path() / "myrenderer-progressive-test";
    const auto stem = directory / "colors";
    writeReferenceImage(image, stem);
    writeReferenceAovs(image, stem);
    int w = 0, h = 0, c = 0;
    auto *hdr = stbi_loadf((stem.string() + ".hdr").c_str(), &w, &h, &c, 3);
    require(hdr && w == 2 && h == 2, "HDR decode failed");
    for (int i = 0; i < 4; ++i)
        near({hdr[i * 3], hdr[i * 3 + 1], hdr[i * 3 + 2]}, image.sum[i]);
    stbi_image_free(hdr);
    auto *png = stbi_load((stem.string() + ".png").c_str(), &w, &h, &c, 3);
    require(png && w == 2 && h == 2, "PNG decode failed");
    require(png[0] == 231 && png[1] == 0 && png[4] == 188 && png[8] == 124 && png[9] == 0,
            "Tone map / sRGB / orientation mismatch");
    const auto guiStaging = makeDisplayRgba8BottomUp(image, RenderOutput::Beauty);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t pngPixel = static_cast<std::size_t>(y * w + x);
            const std::size_t guiPixel = static_cast<std::size_t>((h - 1 - y) * w + x);
            for (int channel = 0; channel < 3; ++channel) {
                require(
                    png[pngPixel * 3U + static_cast<std::size_t>(channel)]
                        == guiStaging[guiPixel * 4U + static_cast<std::size_t>(channel)],
                    "GUI staging and CLI PNG pixels differ"
                );
            }
        }
    }
    stbi_image_free(png);
    auto *normal = stbi_loadf((stem.string() + "-normal.hdr").c_str(), &w, &h, &c, 3);
    require(normal && w == 2 && h == 2, "Normal AOV decode failed");
    near({normal[0], normal[1], normal[2]}, {.5f, .5f, 1.0f}, .01f);
    stbi_image_free(normal);
    auto *sampleCount = stbi_loadf((stem.string() + "-sample-count.hdr").c_str(), &w, &h, &c, 3);
    require(sampleCount && sampleCount[0] == 1.0f, "Sample-count AOV export mismatch");
    stbi_image_free(sampleCount);
    require(image.sum[0].x == 4, "Export mutated HDR");
    for (const std::string suffix : {
             std::string(), std::string("-albedo"), std::string("-normal"),
             std::string("-depth"), std::string("-direct"), std::string("-indirect"),
             std::string("-sample-count"), std::string("-variance")}) {
        std::filesystem::remove(stem.string() + suffix + ".hdr");
        std::filesystem::remove(stem.string() + suffix + ".png");
    }
}
void referenceComparisonOutput() {
    RenderImage image;
    image.width = 2;
    image.height = 2;
    image.completedSamples = 2;
    image.sum = {{8, 0, 0}, {0, 2, 0}, {0, 0, .5f}, {0, 0, 0}};
    image.sampleCounts.assign(4, 2U);
    const auto directory = std::filesystem::temp_directory_path()
        / "myrenderer-reference-comparison-test";
    const auto raster = directory / "raster.png";
    ReferenceComparisonOptions options;
    options.exposure = 1.25f;
    options.sceneName = "Synthetic scene";
    options.targetSamplesPerPixel = 2U;
    options.maxDepth = 5U;
    options.seed = 17U;
    writePathTracedDisplayPng(image, raster, options);
    const ReferenceComparisonMetrics metrics = writeReferenceComparison(
        image, raster, directory, options
    );
    require(metrics.width == 2U && metrics.height == 2U,
            "Reference comparison dimensions mismatch");
    require(metrics.meanAbsoluteError == 0.0
            && metrics.rootMeanSquaredError == 0.0
            && metrics.changedFraction == 0.0
            && std::isinf(metrics.peakSignalToNoiseRatio),
            "Identical reference comparison should have zero error");
    std::ifstream report(directory / "comparison.json", std::ios::binary);
    const std::string reportText{
        std::istreambuf_iterator<char>(report), std::istreambuf_iterator<char>()
    };
    require(reportText.find("\"version\": 2") != std::string::npos
            && reportText.find("\"toneMapping\": \"aces-fitted\"") != std::string::npos
            && reportText.find("\"displayFilter\": \"5x5-median\"") != std::string::npos
            && reportText.find("\"targetSamplesPerPixel\": 2") != std::string::npos
            && reportText.find("\"maxDepth\": 5") != std::string::npos
            && reportText.find("\"seed\": 17") != std::string::npos,
            "Reference comparison reproducibility metadata mismatch");
    report.close();
    for (const char* name : {
             "raster.png", "path-traced.png", "difference-raw.png", "difference.png",
             "triptych.png", "comparison.json"}) {
        require(std::filesystem::is_regular_file(directory / name),
                "Reference comparison artifact is missing");
        std::filesystem::remove(directory / name);
    }
    std::filesystem::remove(directory);
}
void convergence() {
    auto scene = makeDiffuseAcceptanceScene();
    RenderSettings s{12, 12, 512, 5, 42};
    const auto reference = render(scene, s).linearPixels();
    s.samplesPerPixel = 1;
    const auto low = render(scene, s).linearPixels();
    s.samplesPerPixel = 64;
    const auto high = render(scene, s).linearPixels();
    auto bsdfOnlySettings = s;
    bsdfOnlySettings.samplesPerPixel = 8;
    bsdfOnlySettings.nextEventEstimation = false;
    const auto bsdfOnly = render(scene, bsdfOnlySettings).linearPixels();
    s.samplesPerPixel = 8;
    const auto directSampled = render(scene, s).linearPixels();
    double lowError = 0, highError = 0;
    double bsdfOnlyError = 0, directSampledError = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        auto a = low[i] - reference[i], b = high[i] - reference[i];
        const auto c = bsdfOnly[i] - reference[i], d = directSampled[i] - reference[i];
        lowError += glm::dot(a, a);
        highError += glm::dot(b, b);
        bsdfOnlyError += glm::dot(c, c);
        directSampledError += glm::dot(d, d);
    }
    std::cout << "MSE 1/64 SPP vs 512 SPP: " << lowError / (reference.size() * 3) << " / "
              << highError / (reference.size() * 3) << '\n';
    require(highError < lowError, "Fixed-scene error did not decrease");
    std::cout << "MSE BSDF-only/NEE at 8 SPP: " << bsdfOnlyError / (reference.size() * 3) << " / "
              << directSampledError / (reference.size() * 3) << '\n';
    require(directSampledError < bsdfOnlyError, "NEE did not reduce fixed-scene error");
}
} // namespace
void compareAcceptance(const std::string &baseline, const std::string &current) {
    for (const std::string suffix : {
             std::string(), std::string("-albedo"), std::string("-normal"),
             std::string("-depth"), std::string("-direct"), std::string("-indirect"),
             std::string("-sample-count"), std::string("-variance")}) {
      for (const std::string extension : {".hdr", ".png"}) {
        int w = 0, h = 0, c = 0, rw = 0, rh = 0, rc = 0;
        float *a = stbi_loadf((baseline + suffix + extension).c_str(), &w, &h, &c, 3);
        float *b = stbi_loadf((current + suffix + extension).c_str(), &rw, &rh, &rc, 3);
        require(a && b && w == rw && h == rh, "Acceptance decode/dimensions failed");
        double error = 0, energy = 0;
        for (int i = 0; i < w * h * 3; ++i) {
            require(std::isfinite(a[i]) && std::isfinite(b[i]), "Nonfinite acceptance pixels");
            const double delta = a[i] - b[i];
            error += delta * delta;
            energy += a[i] * a[i];
        }
        stbi_image_free(a);
        stbi_image_free(b);
        const double relativeRmse = std::sqrt(error / std::max(energy, 1e-12));
        std::cout << (suffix.empty() ? "beauty" : suffix.substr(1)) << extension
                  << " relative RMSE: " << relativeRmse << '\n';
        require(relativeRmse <= 0.002, "Acceptance differs from committed baseline");
      }
    }
}
int main(int argc, char **argv) {
    try {
        if (argc == 3) {
            compareAcceptance(argv[1], argv[2]);
            return 0;
        }
        require(argc == 1, "Usage: MyRendererProgressiveTests [baseline-stem current-stem]");
        cameraAndSampling();
        energyAndDepth();
        metallicRoughnessBsdf();
        textureSamplingAndMaterialEvaluation();
        dielectricTransmissionAndVolume();
        environmentImportanceSampling();
        environmentCameraVisibility();
        environmentDirectLightingAndVariance();
        directLightSampling();
        terminalDirectLighting();
        accumulationAndTasks();
        aovAccumulation();
        adaptiveSampling();
        tileSchedulingAndProfiles();
        twoLevelInstancing();
        flatHdrDecoding();
        output();
        referenceComparisonOutput();
        convergence();
        std::cout << "Progressive CPU tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
