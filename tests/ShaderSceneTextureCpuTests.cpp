#include "plugin/ShaderScene.h"
#include "asset/InputManifest.h"
#include "render/Renderer.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string read(const std::filesystem::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
}
void testShaderSceneTexturesCpu(const std::filesystem::path& fixture) {
    const auto root = std::filesystem::temp_directory_path() / ("IrisTextureCpu-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        for (const auto& file : std::filesystem::directory_iterator(fixture))
            if (file.is_regular_file()) std::filesystem::copy_file(file.path(), root / file.path().filename());
        const auto manifest = root / "textures.shaderscene";
        const auto valid = read(manifest);
        const auto write = [&](const std::string& text) { std::ofstream(manifest, std::ios::trunc) << text; };
        iris::ShaderScenePackage package; std::string error;
        {
            capture::InputManifest inputs;
            check(iris::loadShaderScenePackage(manifest, package, error), error.c_str());
            check(inputs.files.size() == 3, "Texture input manifest must contain JSON, PNG and raw"); inputs.validate();
        }
        std::size_t bytes = 0; for (const auto& texture : package.textures) bytes += iris::shaderSceneTextureBytes(texture);
        check(bytes == 38109 && package.textures[0].texels.size() == 256 && package.textures[1].texels.size() == 64,
            "Texture staging or Mip budget wrong");
        check(package.textures[0].texels[0] == 128 && package.textures[0].texels[1] == 64 && package.textures[0].texels[2] == 192,
            "PNG decoding/orientation wrong");
        check(package.textures[1].texels[0] == 16 && package.textures[1].texels[63] == 232, "Raw voxel order lost");
        const auto original = package.textures[2].texels;
        iris::ShaderSceneTexture noise; noise.volume = true; noise.width = noise.height = noise.depth = 32;
        noise.encoding = iris::ShaderSceneTextureEncoding::HashV1;
        iris::loadShaderSceneTextureData(noise);
        check(noise.texels.size() == 32768 && noise.fingerprint == "c7c241b345c4317c", "hash-v1 canonical byte identity changed");
        noise.seed = 17; iris::loadShaderSceneTextureData(noise); check(noise.texels == original, "Noise Seed not reproducible");
        noise.seed = 18; iris::loadShaderSceneTextureData(noise); check(noise.texels != original, "Noise Seed has no contribution");
        const auto reject = [&](std::string text, const char* from, const char* to) {
            const auto position = text.find(from); check(position != std::string::npos, "CPU test replacement not found");
            text.replace(position, std::string(from).size(), to); write(text);
            check(!iris::loadShaderScenePackage(manifest, package, error) && !error.empty(), "Invalid texture package accepted");
            check(package.id == "textureGallery" && package.textures[2].texels == original, "Rejected texture replaced prior package");
        };
        reject(valid, "\"version\": 3", "\"version\": 2");
        reject(valid, "\"textures\": [", "\"textures\": [null,");
        reject(valid, "\"textures\": [", "\"textures\": [[],");
        reject(valid, "\"seed\": 17", "\"seed\": -1");
        reject(valid, "\"seed\": 17", "\"seed\": 4294967296");
        reject(valid, "\"seed\": 17", "\"seed\": 17, \"path\": \"volume.raw\"");
        reject(valid, "\"id\": \"Grain\"", "\"id\": \"Palette\"");
        reject(valid, "\"id\": \"Grain\"", "\"id\": \"Image\"");
        reject(valid, "\"extent\": [4, 4, 4]", "\"extent\": [4, 4]");
        reject(valid, "\"extent\": [4, 4, 4]", "\"extent\": [129, 4, 4]");
        reject(valid, "\"extent\": [4, 4, 4]", "\"extent\": [0, 4, 4]");
        reject(valid, "\"extent\": [8, 8]", "\"extent\": [7, 8]");
        reject(valid, "\"format\": \"linear-r8\"", "\"format\": \"srgb-rgba8\"");
        reject(valid, "\"path\": \"volume.raw\"", "\"path\": \"../outside.raw\"");
        reject(valid, "\"path\": \"volume.raw\"", "\"path\": \"C:/outside.raw\"");
        reject(valid, "\"source\": \"Palette\", \"frame\": \"current\"", "\"source\": \"Palette\", \"frame\": \"previous\"");
        reject(valid, "\"source\": \"Volume\", \"frame\": \"current\", \"filter\": \"linear\", \"wrap\": \"clamp\", \"mip\": \"none\"",
            "\"source\": \"Volume\", \"frame\": \"current\", \"filter\": \"linear\", \"wrap\": \"clamp\", \"mip\": \"linear\"");
        write(valid);
        auto raw = read(root / "volume.raw"); const auto stamp = std::filesystem::last_write_time(root / "volume.raw");
        raw[0] ^= 1; { std::ofstream(root / "volume.raw", std::ios::binary | std::ios::trunc) << raw; }
        std::filesystem::last_write_time(root / "volume.raw", stamp);
        check(!iris::shaderSceneTextureInputsCurrent(package), "Same-size/same-mtime content change not detected");
        { std::ofstream(root / "volume.raw", std::ios::binary | std::ios::trunc) << "short"; }
        check(!iris::loadShaderScenePackage(manifest, package, error) && package.textures[2].texels == original, "Raw truncation not transactional");
        std::filesystem::copy_file(fixture / "volume.raw", root / "volume.raw", std::filesystem::copy_options::overwrite_existing);
        { std::ofstream(root / "palette.png", std::ios::binary | std::ios::trunc) << "invalid png"; }
        check(!iris::loadShaderScenePackage(manifest, package, error), "Corrupt PNG accepted");
        std::filesystem::copy_file(fixture / "palette.png", root / "palette.png", std::filesystem::copy_options::overwrite_existing);
        check(iris::loadShaderScenePackage(manifest, package, error) && iris::shaderSceneTextureInputsCurrent(package), "Texture recovery failed");
        std::string resources;
        for (int i = 0; i < 4; ++i) {
            if (i) resources += ',';
            resources += "{\"id\":\"T" + std::to_string(i) + R"(","dimension":"2d","format":"linear-rgba8","extent":[2048,2048],"encoding":"raw","path":"volume.raw","mip":"generate"})";
        }
        write(R"({"version":3,"id":"budget","resourceRoot":".","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[],"textures":[)" + resources
            + R"(],"passes":[{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[]}]})");
        check(!iris::loadShaderScenePackage(manifest, package, error) && error.find("64 MiB") != std::string::npos,
            "Texture budget not rejected before decoding");
        const std::string sourcePackage = R"({"version":3,"id":"source","resourceRoot":".","entry":"mainImage","camera":"project","author":"test","license":"MIT","textures":[],"parameterSource":{"pluginId":"iris.enscape-study","schemaVersion":1},"parameters":[{"id":"noiseReduction","label":"Noise","type":"bool","default":false},{"id":"waveHeight","label":"Wave","type":"float","default":0.6,"min":0.05,"max":1.5}],"passes":[{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[]}]})";
        write(sourcePackage); check(iris::loadShaderScenePackage(manifest, package, error), error.c_str());
        RendererSettings sourceSettings; sourceSettings.enscapeCube.noiseReduction = true; sourceSettings.enscapeCube.waveHeight = 0.9f;
        const auto sourceValues = iris::shaderSceneParameterValues(package, sourceSettings);
        check(sourceValues[0].boolean && sourceValues[1].number == 0.9f && !package.parameters.value("noiseReduction")->boolean,
            "CPU schema source did not preserve one live value source");
        ModuleParameterOverride bound; bound.id="noiseReduction"; bound.value.type=ModuleParameterType::Bool; bound.value.boolean=false;
        bool changed=false;
        check(iris::setShaderSceneParameter(package,sourceSettings,bound,changed,error) && changed
            && !sourceSettings.enscapeCube.noiseReduction && sourceSettings.shaderSceneParameters.empty(),"Bound UI created a parallel value source");
        bound.id="not-public";
        check(!iris::setShaderSceneParameter(package,sourceSettings,bound,changed,error),"Bound private parameter accepted");
        const auto badSource = [&](const char* from, const char* to) {
            auto text = sourcePackage; text.replace(text.find(from), std::string(from).size(), to); write(text);
            check(!iris::loadShaderScenePackage(manifest, package, error) && package.id == "source", "Bad source schema replaced prior package");
        };
        badSource("\"schemaVersion\":1", "\"schemaVersion\":2");
        badSource("iris.enscape-study", "missing.schema");
        badSource("\"default\":false", "\"default\":0");
        badSource("\"max\":1.5", "\"max\":1.0");
        sourceSettings.enscapeCube.waveHeight = 100;
        bool rejectedSource = false;
        try { iris::shaderSceneParameterValues(package, sourceSettings); } catch (const std::exception&) { rejectedSource = true; }
        check(rejectedSource, "Out-of-range source value accepted");
        sourceSettings.enscapeShaderSceneResource = "package.shaderscene";
        check(iris::shaderSceneResourceFor(sourceSettings).empty(), "Disabled compatibility selector remained active");
        sourceSettings.enscapeCubeShaderEnabled = true;
        check(iris::shaderSceneResourceFor(sourceSettings) == "package.shaderscene", "Compatibility selector lost resource");
        std::cout << "Texture CPU v3 / PNG+raw / Seed+golden identity / strict fields / bounds+budget / transactional recovery PASS\n";
    } catch (...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
}
