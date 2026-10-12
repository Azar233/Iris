#include "plugin/ShaderSceneRenderer.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(data.data(), static_cast<std::streamsize>(data.size()));
    static int revision = 400;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++revision));
}
}
void testShaderSceneReload(const std::filesystem::path& shaders, const std::filesystem::path& directory, unsigned int vao) {
    const auto root = directory / "reload"; std::filesystem::create_directories(root);
    const auto manifest = root / "reload.shaderscene", fragment = root / "image.frag", raw = root / "data.raw";
    const auto description = [](const std::string& texture, const std::string& fragmentName = "image.frag") {
        return std::string(R"({"version":3,"id":"reload","resourceRoot":".","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.5,"min":0,"max":1}],"textures":[{"id":"Tex","dimension":"2d","format":"linear-r8","extent":[1,1],"encoding":"raw","path":")") + texture
            + R"(","mip":"none"}],"passes":[{"id":"Image","fragment":")" + fragmentName
            + R"(","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Tex","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"}]}]})";
    };
    const std::string source = "#version 330 core\n#include \"value.glsl\"\nuniform sampler2D irisChannel0;uniform float gain;out vec4 color;void main(){color=vec4(texture(irisChannel0,vec2(.5)).r,gain,value(),1);}\n";
    write(manifest, description("data.raw")); write(fragment, source); write(raw, std::string(1, 64));
    write(root / "value.glsl", "float value(){return .25;}\n");
    RendererSettings settings; settings.shaderSceneResource = manifest.u8string();
    ModuleParameterValue gain; gain.number = .75f;
    settings.shaderSceneParameters.push_back({"gain", gain});
    Camera camera; RenderTarget target; target.resize(16, 8, 1);
    iris::ShaderSceneRenderer runtime(shaders); runtime.prepareResources(settings, 16, 8);
    const auto draw = [&]() {
        runtime.renderFrame({target, camera, settings, 16, 8, .25f, vao});
        std::vector<unsigned char> pixels(16U*8U*4U); glBindTexture(GL_TEXTURE_2D, target.colorTexture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        check(glGetError() == GL_NO_ERROR, "Reload GL error"); return pixels;
    };
    auto baseline = draw(); check(baseline[0] == 64 && baseline[1] == 191 && baseline[2] == 64, "Reload fixture inputs missing");
    const auto reject = [&]() {
        const auto before = runtime.package(); const auto history = runtime.runtimeStatus();
        const auto report = runtime.reloadResources(settings);
        check(report.failed == 1 && report.pending == 1 && runtime.package() == before, "Rejected reload replaced package");
        check(runtime.runtimeStatus().frame == history.frame && !runtime.runtimeStatus().resetPending && baseline == draw(), "Rejected reload lost pixels/history");
        const auto idle = runtime.reloadResources(settings);
        check(idle.failed == 0 && idle.reloaded == 0 && idle.pending == 1, "Unchanged bad package retried repeatedly");
        check(runtime.reloadResources(settings, true).failed == 1, "Explicit retry did not reattempt rejected package");
    };
    const auto accept = [&]() {
        const auto report = runtime.reloadResources(settings);
        check(report.failed == 0 && report.reloaded == 1 && runtime.runtimeStatus().resetPending, "Changed package not atomically published/reset");
        baseline = draw(); check(baseline[1] == 191, "Reload discarded authored parameter override");
    };
    write(raw, std::string(1, static_cast<char>(192))); accept(); check(baseline[0] == 192, "Raw change not consumed");
    write(raw, "bad"); reject(); write(raw, std::string(1, 64)); accept();
    std::filesystem::remove(raw); reject(); write(raw, std::string(1, 64)); accept();
    write(manifest, "{broken"); reject(); write(manifest, description("data.raw")); accept();
    std::filesystem::remove(manifest); reject(); write(manifest, description("data.raw")); accept();
    // New dependencies absent from the committed package must recover without touching Manifest again.
    write(manifest, description("new.raw")); reject(); write(root / "new.raw", std::string(1, 32)); accept();
    check(baseline[0] == 32 && runtime.package()->textures[0].path.filename() == "new.raw", "New texture dependency not published");
    write(fragment, "#version 330 core\n#include \"new.glsl\"\nuniform float gain;out vec4 color;void main(){color=vec4(0,gain,value(),1);}\n");
    reject(); write(root / "new.glsl", "float value(){return .5;}\n"); accept(); check(baseline[2] >= 127 && baseline[2] <= 128, "Missing include creation did not recover");
    write(root / "new.glsl", "bad include\n"); reject(); write(root / "new.glsl", "float value(){return .75;}\n"); accept();
    write(manifest, description("new.raw", "new.frag")); reject(); write(root / "new.frag", source); accept();
    check(runtime.package()->passes[0].fragment.filename() == "new.frag", "Manifest fragment switch not published");
    auto badSchema = description("new.raw", "new.frag");
    const auto offset = badSchema.find("\"max\":1"); badSchema.replace(offset, 7, "\"max\":0.6");
    write(manifest, badSchema); reject(); write(manifest, description("new.raw", "new.frag")); accept();
    write(root / "README.md", "Unrelated documentation");
    check(runtime.reloadResources(settings).reloaded == 0, "Unrelated documentation triggered rebuild");
    write(root / "new.frag", "#version 330 core\ninvalid program\n");
    check(Shader::reloadChangedShaders().failed == 0 && Shader::reloadChangedShaders().reloaded == 0,
        "Package-owned shader escaped into global program reload");
    reject();
    std::cout << "Whole package reload: Manifest/raw/source/include, new/missing inputs, overrides, pending/no-spin/retry, atomic retention/reset PASS\n";
}
