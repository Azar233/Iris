#include "plugin/ShaderSceneRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::trunc) << text;
    static int revision = 100;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++revision));
}
std::vector<unsigned char> pixels(const RenderTarget& target) {
    std::vector<unsigned char> data(static_cast<std::size_t>(target.width() * target.height() * 4));
    glBindTexture(GL_TEXTURE_2D, target.colorTexture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    check(glGetError() == GL_NO_ERROR, "mainImage GL error"); return data;
}
}

void testShaderSceneEntry(const std::filesystem::path& shaders, const std::filesystem::path& parent, unsigned int vao) {
    const auto root = parent / "entry"; std::filesystem::create_directories(root);
    const auto manifest = [&](const std::string& entry) {
        return std::string(R"({"version":2,"id":"entry","resourceRoot":".","entry":")") + entry
            + R"(","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.125,"min":0,"max":1}],"passes":[{"id":"Image","fragment":")" + entry
            + R"(-image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Accum","frame":"current","filter":"linear","wrap":"clamp","mip":"none"}]},{"id":"Accum","fragment":")" + entry
            + R"(-accum.frag","output":"linear-rgba16f","sizeDivisor":2,"channels":[{"slot":0,"source":"Accum","frame":"previous","filter":"nearest","wrap":"clamp","mip":"none"}]}]})";
    };
    const std::string accum = "#version 330 core\nuniform sampler2D irisChannel0;uniform vec2 irisResolution;uniform float irisTimeDelta;uniform float gain;\n";
    const std::string accumBody = "color=texture(irisChannel0,coord/irisResolution)+vec4(gain,irisTimeDelta,0,0);";
    const std::string image = "#version 330 core\n#include \"color.glsl\"\nuniform sampler2D irisChannel0;uniform vec2 irisResolution;uniform float irisTime;uniform vec3 irisCameraPosition;\n";
    const std::string imageBody = "color=texture(irisChannel0,coord/irisResolution)*tint();color.b=irisTime+irisCameraPosition.x*0.01; color.a=1.0;";
    write(root / "color.glsl", "vec4 tint(){return vec4(1.0);}\n");
    write(root / "main-accum.frag", accum + "out vec4 color;void main(){vec2 coord=gl_FragCoord.xy;" + accumBody + "}\n");
    write(root / "main-image.frag", image + "out vec4 color;void main(){vec2 coord=gl_FragCoord.xy;" + imageBody + "}\n");
    const auto wrappedAccum = accum + "void mainImage(out vec4 color,in vec2 coord){" + accumBody + "}\n";
    const auto wrappedImage = image + "void mainImage(out vec4 color,in vec2 coord){" + imageBody + "}\n// trailing comment";
    write(root / "mainImage-accum.frag", wrappedAccum); write(root / "mainImage-image.frag", wrappedImage);
    write(root / "main.shaderscene", manifest("main")); write(root / "mainImage.shaderscene", manifest("mainImage"));
    RendererSettings originalSettings, adaptedSettings;
    originalSettings.shaderSceneResource = (root / "main.shaderscene").generic_u8string();
    adaptedSettings.shaderSceneResource = (root / "mainImage.shaderscene").generic_u8string();
    Camera camera; RenderTarget originalTarget, adaptedTarget;
    originalTarget.resize(64, 32, 1); adaptedTarget.resize(64, 32, 1);
    iris::ShaderSceneRenderer original(shaders), adapted(shaders);
    original.prepareResources(originalSettings, 64, 32); adapted.prepareResources(adaptedSettings, 64, 32);
    check(original.estimatedBytes() == adapted.estimatedBytes() && original.frameInfo().drawCalls == adapted.frameInfo().drawCalls,
        "mainImage changed resource/draw budget");
    const auto draw = [&](iris::ShaderSceneRenderer& renderer, RenderTarget& target, const RendererSettings& settings, float time) {
        renderer.renderFrame({target, camera, settings, target.width(), target.height(), time, vao}); return pixels(target);
    };
    for (int frame = 0; frame < 24; ++frame) {
        const float time = static_cast<float>(frame) / 24;
        const auto reference = draw(original, originalTarget, originalSettings, time);
        check(reference == draw(adapted, adaptedTarget, adaptedSettings, time), "mainImage differs from main over feedback sequence");
        check(reference == draw(adapted, adaptedTarget, adaptedSettings, time), "mainImage paused redraw accumulated feedback");
    }
    adapted.invalidateHistory(); const auto baseline = draw(adapted, adaptedTarget, adaptedSettings, 0.25f);
    const auto reject = [&](const std::string& bad, const char* diagnostic = "mainImage-image.frag") {
        write(root / "mainImage-image.frag", bad);
        const auto report = Shader::reloadChangedShaders(true);
        check(report.failed > 0 && report.retained > 0 && report.message.find(diagnostic) != std::string::npos,
            "mainImage failure missing retention/path diagnostic");
        check(baseline == draw(adapted, adaptedTarget, adaptedSettings, 0.25f), "mainImage failure replaced live image");
    };
    reject(image + "void mainImage(out vec4 color){color=vec4(1);}\n");
    reject(image + "void main(){ }\n"); // Missing mainImage / duplicate main.
    reject(wrappedImage + "\nvoid main(){}\n");
    write(parent / "outside-entry.glsl", "vec4 outsideColor(){return vec4(1);}\n");
    reject("#version 330 core\n#include \"../outside-entry.glsl\"\nvoid mainImage(out vec4 color,in vec2 coord){color=outsideColor();}\n", "outside-entry.glsl");
    reject("#version 330 core\nuniform vec3 irisTime;void mainImage(out vec4 color,in vec2 coord){color=vec4(irisTime,1);}\n");
    write(root / "mainImage-image.frag", wrappedImage);
    auto report = Shader::reloadChangedShaders(true);
    check(report.failed == 0 && report.reloaded > 0, "mainImage retry failed");
    check(baseline == draw(adapted, adaptedTarget, adaptedSettings, 0.25f), "mainImage recovery changed output");
    draw(adapted, adaptedTarget, adaptedSettings, 0.3f);
    write(root / "color.glsl", "vec4 tint(){return vec4(0.5,1.0,1.0,1.0);}\n");
    report = Shader::reloadChangedShaders();
    check(report.failed == 0 && report.reloaded == 2, "Included mainImage source not watched");
    const auto changed = draw(adapted, adaptedTarget, adaptedSettings, 0.3f);
    check(changed[0] >= 15 && changed[0] <= 16 && changed[1] == 0, "mainImage reload failed to reset history/use changed include");
    camera.moveLocal(0, 1); check(changed != draw(adapted, adaptedTarget, adaptedSettings, 0.3f), "mainImage camera not consumed");
    adaptedTarget.resize(31, 47, 1);
    const auto resized = draw(adapted, adaptedTarget, adaptedSettings, 0.3f);
    check(resized.size() == 31U * 47U * 4U && resized == draw(adapted, adaptedTarget, adaptedSettings, 0.3f), "mainImage resize/repeat failed");
    write(root / "mainImage-image.frag", "#version 330 core\nvoid mainImage(out vec4 color,in vec2 coord){color=vec4(0,1,0,1);}\n");
    adapted.prepareResources(adaptedSettings, 31, 47);
    const auto replaced = draw(adapted, adaptedTarget, adaptedSettings, 0.3f);
    check(replaced[0] == 0 && replaced[1] == 255 && replaced[2] == 0 && replaced[3] == 255, "mainImage prepared replacement failed");
    write(root / "mainImage-image.frag", "#version 330 core\nvoid mainImage(out vec4 color,in vec2 coord){invalid}\n");
    bool rejected = false;
    try { adapted.prepareResources(adaptedSettings, 31, 47); } catch (const std::exception&) { rejected = true; }
    check(rejected && replaced == draw(adapted, adaptedTarget, adaptedSettings, 0.3f), "mainImage rebuild lost live package");
    std::cout << "mainImage main parity / 24-frame feedback / pause / signature+duplicate+type rejection / include reload / history / camera / resize / rebuild PASS\n";
}
