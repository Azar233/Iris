#include "plugin/ShaderSceneRenderer.h"
#include "render/Shader.h"
#include "render/Renderer.h"
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
void write(const std::filesystem::path& path, const std::string& source) {
    std::ofstream(path, std::ios::trunc) << source;
    static int revision = 20;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++revision));
}
}
void testShaderSceneMultiPass(const std::filesystem::path& shaders, const std::filesystem::path& directory, unsigned int vao) {
    const auto manifest = directory / "multi.shaderscene";
    const auto accum = directory / "accum.frag", copy = directory / "copy.frag", image = directory / "display.frag";
    const std::string description = R"({"version":2,"id":"multi","resourceRoot":".","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[],"passes":[
      {"id":"Image","fragment":"display.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Copy","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"}]},
      {"id":"Copy","fragment":"copy.frag","output":"linear-rgba16f","sizeDivisor":1,"channels":[{"slot":0,"source":"Accum","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"},{"slot":1,"source":"Accum","frame":"previous","filter":"linear","wrap":"repeat","mip":"none"}]},
      {"id":"Accum","fragment":"accum.frag","output":"linear-rgba16f","sizeDivisor":2,"channels":[{"slot":0,"source":"Copy","frame":"previous","filter":"nearest","wrap":"clamp","mip":"none"}]}]})";
    const std::string accumulation = "#version 330 core\nout vec4 color;uniform sampler2D irisChannel0;uniform vec2 irisResolution;uniform int irisFrame;uniform bool irisHistoryValid;void main(){color=vec4(texture(irisChannel0,gl_FragCoord.xy/irisResolution).r + 0.125,float(irisFrame)/16.0,irisHistoryValid?1.0:0.0,1);}\n";
    const std::string copying = "#version 330 core\nout vec4 color;uniform sampler2D irisChannel0,irisChannel1;uniform vec2 irisResolution,irisViewportResolution,irisChannelResolution0;void main(){vec2 uv=gl_FragCoord.xy/irisResolution;color=vec4(texture(irisChannel0,uv).r,texture(irisChannel1,uv).r,texture(irisChannel0,uv).b,1);if(irisChannelResolution0.x!=ceil(irisViewportResolution.x/2.0)||irisChannelResolution0.y!=ceil(irisViewportResolution.y/2.0))color=vec4(1,0,1,1);}\n";
    write(manifest, description); write(accum, accumulation); write(copy, copying);
    write(image, "#version 330 core\nout vec4 color;uniform sampler2D irisChannel0;uniform vec2 irisResolution;void main(){color=texture(irisChannel0,gl_FragCoord.xy/irisResolution);}\n");
    RendererSettings settings; settings.shaderSceneResource = manifest.generic_u8string();
    Camera camera; RenderTarget target; target.resize(64, 32, 1);
    iris::ShaderSceneRenderer runtime(shaders); runtime.prepareResources(settings, 64, 32);
    check(runtime.frameInfo().passNames == std::vector<std::string>({"ShaderScene Accum", "ShaderScene Copy", "ShaderScene Image"}), "CPU graph was not executed topologically");
    check(runtime.estimatedBytes() == (64U*32U + 32U*16U)*16U, "Private ping-pong bytes incorrect");
    const auto draw = [&](float time) {
        runtime.renderFrame({target, camera, settings, target.width(), target.height(), time, vao});
        check(glGetError() == GL_NO_ERROR, "Multi Pass GL error");
        std::vector<unsigned char> pixels(static_cast<std::size_t>(target.width()*target.height()*4));
        glBindTexture(GL_TEXTURE_2D, target.colorTexture()); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); return pixels;
    };
    const auto first = draw(0); check(first[0] == 32 && first[1] == 0 && first[2] == 0, "History was not initially zero");
    check(first == draw(0), "Repeated frame advanced feedback");
    const auto second = draw(0.05f); check(second[0] == 64 && second[1] == 32 && second[2] == 255, "Current/previous channel sides aliased");
    check(second == draw(0.05f), "Repeated time changed history");
    write(accum, "#version 330 core\nout vec4 color;void main(){color=vec4(0.25,0,0,1);}\n");
    write(copy, "#version 330 core\ninvalid multi Pass\n");
    const auto failed = runtime.reloadResources(settings);
    check(failed.failed > 0 && failed.retained >= 2 && second == draw(0.05f), "Partial multi Pass compile replaced live graph/history");
    bool rejected = false;
    try { runtime.prepareResources(settings, 64, 32); } catch (const std::exception&) { rejected = true; }
    check(rejected && second == draw(0.05f), "Failed whole-package rebuild lost live graph");
    write(copy, copying);
    const auto recovered = runtime.reloadResources(settings, true);
    check(recovered.failed == 0 && recovered.reloaded >= 2, "Multi Pass retry failed");
    const auto resetAfterReload = draw(0.05f);
    check(resetAfterReload[0] == 64 && resetAfterReload[1] == 0, "Successful reload retained stale feedback");
    write(accum, accumulation); check(runtime.reloadResources(settings, true).failed == 0, "Original feedback restore failed");
    runtime.invalidateHistory(); check(first == draw(0.1f), "Explicit reset did not clear both sides");
    draw(0.15f); check(first == draw(0.1f), "Backward seek retained stale feedback");
    draw(0.15f); check(first == draw(1.0f), "Large time step retained feedback");
    draw(1.05f); camera.moveLocal(0, 1); check(first == draw(1.05f), "Camera change retained history");
    camera.reset(); target.resize(31, 47, 1); const auto resized = draw(1.1f);
    check(resized[0] == 32 && resized[1] == 0 && resized[2] == 0, "Odd half-size or resize reset failed");
    const auto contexts = runtime.passContexts(); check(contexts[0].viewportWidth == 16 && contexts[0].viewportHeight == 24, "Pass dimensions misreported");
    rejected = false;
    try { runtime.prepareResources(settings, 8192, 8192); } catch (const std::exception&) { rejected = true; }
    check(rejected && resized == draw(1.1f), "Budget rejection lost prior output");
    const auto sequence = [&]() {
        runtime.invalidateHistory(); std::vector<std::vector<unsigned char>> frames;
        for (int i = 0; i < 24; ++i) frames.push_back(draw(static_cast<float>(i) / 24));
        return frames;
    };
    check(sequence() == sequence(), "Feedback sequence not reproducible from clear initial state");
    // Sampling assertions use a two-texel ramp, not a constant texture.
    const auto sampleManifest = directory / "sampling.shaderscene";
    write(directory / "pattern.frag", "#version 330 core\nout vec4 color;uniform vec2 irisResolution;void main(){color=vec4(gl_FragCoord.x/irisResolution.x,0,0,1);}\n");
    const auto sampling = [&](const std::string& filter, const std::string& wrap, const std::string& u) {
        write(directory / "sample.frag", "#version 330 core\nout vec4 color;uniform sampler2D irisChannel0;void main(){color=texture(irisChannel0,vec2(" + u + ",0.5));}\n");
        write(sampleManifest, R"({"version":2,"id":"sampling","resourceRoot":".","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[],"passes":[{"id":"Pattern","fragment":"pattern.frag","output":"linear-rgba16f","sizeDivisor":1,"channels":[]},{"id":"Image","fragment":"sample.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Pattern","frame":"current","filter":")" + filter + "\",\"wrap\":\"" + wrap + R"(","mip":"none"}]}]})");
        RendererSettings sampled; sampled.shaderSceneResource = sampleManifest.generic_u8string();
        RenderTarget sampledTarget; sampledTarget.resize(2, 1, 1);
        iris::ShaderSceneRenderer sampledRuntime(shaders); sampledRuntime.prepareResources(sampled, 2, 1);
        sampledRuntime.renderFrame({sampledTarget, camera, sampled, 2, 1, 0, vao});
        std::array<unsigned char, 8> data{}; glBindTexture(GL_TEXTURE_2D, sampledTarget.colorTexture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
        check(glGetError() == GL_NO_ERROR, "Sampling GL error"); return data[0];
    };
    check(sampling("nearest", "clamp", "0.5") == 191, "Nearest sampling not applied");
    const auto linear = sampling("linear", "clamp", "0.5"); check(linear >= 127 && linear <= 128, "Linear sampling not applied");
    check(sampling("nearest", "clamp", "1.25") == 191 && sampling("nearest", "repeat", "1.25") == 64, "Clamp/repeat sampling not applied");
    std::cout << "Multi Pass graph/order/current+previous/half-size/repeat/24-frame replay/atomic retry/reset/resize/budget PASS\n";
}
