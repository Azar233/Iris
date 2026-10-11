#include "plugin/ShaderSceneRenderer.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include "asset/InputManifest.h"
#include <glad/gl.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& text) { std::ofstream(path, std::ios::binary | std::ios::trunc) << text; }
void writeBytes(const std::filesystem::path& path, std::initializer_list<unsigned char> bytes) {
    const std::vector<unsigned char> data(bytes); std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
std::vector<unsigned char> pixels(const RenderTarget& target) {
    std::vector<unsigned char> bytes(static_cast<std::size_t>(target.width() * target.height() * 4));
    glBindTexture(GL_TEXTURE_2D, target.colorTexture()); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    check(glGetError() == GL_NO_ERROR, "Texture GPU GL error"); return bytes;
}
std::string manifest(const std::string& texture, const std::string& filter, const std::string& wrap, const std::string& mip = "none") {
    return R"({"version":3,"id":"textureTest","resourceRoot":".","entry":"mainImage","camera":"project","author":"test","license":"MIT","parameters":[],"textures":[)" + texture
        + R"(],"passes":[{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Tex","frame":"current","filter":")"
        + filter + "\",\"wrap\":\"" + wrap + "\",\"mip\":\"" + mip + "\"}]}]}";
}
}
void testShaderSceneTexturesGpu(const std::filesystem::path& shaders, const std::filesystem::path& parent, unsigned int vao) {
    const auto root = parent / "textures"; std::filesystem::create_directories(root);
    const auto fixture = std::filesystem::absolute(shaders).parent_path() / "assets/shader-scenes/textures";
    const auto path = root / "test.shaderscene";
    RendererSettings settings; settings.shaderSceneResource = path.generic_u8string();
    Camera camera; RenderTarget target; target.resize(8, 4, 1);
    iris::ShaderSceneRenderer runtime(shaders);
    const auto draw = [&](float time = 0.0f) {
        runtime.renderFrame({target, camera, settings, target.width(), target.height(), time, vao}); return pixels(target);
    };
    const auto sample = [&](const std::string& texture, const std::string& shader, const std::string& filter,
        const std::string& wrap, const std::string& mip = "none") {
        write(root / "image.frag", "#version 330 core\n" + shader);
        write(path, manifest(texture, filter, wrap, mip)); runtime.prepareResources(settings, target.width(), target.height()); return draw();
    };
    const std::string plane = R"({"id":"Tex","dimension":"2d","format":"linear-r8","extent":[2,1],"encoding":"raw","path":"data.raw","mip":"none"})";
    writeBytes(root / "data.raw", {64, 192});
    const auto samplePlane = [&](const char* coordinate, const char* filter, const char* wrap) {
        return sample(plane, std::string("uniform sampler2D irisChannel0;void mainImage(out vec4 color,in vec2 coord){color=vec4(texture(irisChannel0,vec2(")
            + coordinate + ",0.5)).rrr,1);}", filter, wrap)[0];
    };
    check(samplePlane("0.5", "nearest", "clamp") == 192, "External 2d nearest failed");
    check(samplePlane("0.5", "linear", "clamp") == 128, "External 2d linear failed");
    check(samplePlane("1.25", "nearest", "clamp") == 192 && samplePlane("1.25", "nearest", "repeat") == 64,
        "External 2d wrap failed");
    const std::string volume = R"({"id":"Tex","dimension":"3d","format":"linear-r8","extent":[2,1,2],"encoding":"raw","path":"data.raw","mip":"generate"})";
    writeBytes(root / "data.raw", {0, 64, 128, 255});
    const auto sampleVolume = [&](const char* coordinate, const char* filter, const char* wrap, const char* mip = "none", bool lod = false) {
        return sample(volume, std::string("uniform sampler3D irisChannel0;uniform vec3 irisChannelExtent0;void mainImage(out vec4 color,in vec2 coord){color=vec4(")
            + (lod ? "textureLod" : "texture") + "(irisChannel0,vec3(" + coordinate + (lod ? "),10.0)" : "))")
            + ".r,irisChannelExtent0.z/2.0,irisChannelExtent0.x/2.0,1);}", filter, wrap, mip);
    };
    const auto trilinear = sampleVolume("0.5,0.5,0.5", "linear", "clamp");
    check(trilinear[0] >= 111 && trilinear[0] <= 112 && trilinear[1] == 255 && trilinear[2] == 255,
        "External 3d interpolation or extent failed");
    check(sampleVolume("0.25,0.5,1.25", "nearest", "clamp")[0] == 128
        && sampleVolume("0.25,0.5,1.25", "nearest", "repeat")[0] == 0, "3d R-axis clamp/repeat failed");
    const auto mip = sampleVolume("0.75,0.5,0.75", "linear", "clamp", "linear", true)[0];
    check(mip >= 111 && mip <= 112 && runtime.estimatedBytes() == 5, "3d Mip chain/byte budget failed");
    check(sampleVolume("0.75,0.5,0.75", "nearest", "clamp", "none", true)[0] == 255, "No-Mip sampler unexpectedly selected Mip");
    const std::string rgbaVolume = R"({"id":"Tex","dimension":"3d","format":"linear-rgba8","extent":[1,1,2],"encoding":"raw","path":"rgba.raw","mip":"none"})";
    writeBytes(root / "rgba.raw", {20, 40, 60, 80, 100, 120, 140, 160});
    const auto rgba = sample(rgbaVolume, "uniform sampler3D irisChannel0;void mainImage(out vec4 c,in vec2 p){c=texture(irisChannel0,vec3(0.5));}", "linear", "clamp");
    check(rgba[0] == 60 && rgba[1] == 80 && rgba[2] == 100 && rgba[3] == 120, "Raw RGBA3d component order/interpolation wrong");
    sampleVolume("0.75,0.5,0.75", "nearest", "clamp", "none", true);
    const auto baseline = draw();
    write(root / "image.frag", "#version 330 core\nuniform sampler2D irisChannel0;void mainImage(out vec4 c,in vec2 p){c=texture(irisChannel0,vec2(0.5));}");
    bool rejected = false; try { runtime.prepareResources(settings, 8, 4); } catch (const std::exception&) { rejected = true; }
    check(rejected && baseline == draw(), "3d sampler mismatch lost live output");
    std::filesystem::copy_file(fixture / "palette.png", root / "palette.png");
    const auto png = [&](const char* format) {
        return "{\"id\":\"Tex\",\"dimension\":\"2d\",\"format\":\"" + std::string(format)
            + R"(","extent":[8,8],"encoding":"png","path":"palette.png","mip":"generate"})";
    };
    const std::string pngShader = "uniform sampler2D irisChannel0;void mainImage(out vec4 c,in vec2 p){c=texture(irisChannel0,vec2(0.0625));}";
    const auto linear = sample(png("linear-rgba8"), pngShader, "nearest", "repeat");
    check(linear[0] == 128 && linear[1] == 64 && linear[2] == 192 && linear[3] == 255, "PNG RGBA upload wrong");
    const auto srgb = sample(png("srgb-rgba8"), pngShader, "nearest", "repeat");
    check(srgb[0] >= 54 && srgb[0] <= 56 && srgb[1] >= 12 && srgb[1] <= 14 && srgb[2] >= 133 && srgb[2] <= 135,
        "PNG sRGB decode not linear");
    // Hostile unpack state must neither shift CPU pixels nor remain changed after preparation.
    GLuint unpack = 0, marker2d = 0, marker3d = 0;
    glGenBuffers(1, &unpack); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack); glBufferData(GL_PIXEL_UNPACK_BUFFER, 64, nullptr, GL_STATIC_DRAW);
    glGenTextures(1, &marker2d); glGenTextures(1, &marker3d);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, marker2d); glBindTexture(GL_TEXTURE_3D, marker3d);
    const std::array<GLenum, 6> fields{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES};
    const std::array<GLint, 6> values{8, 19, 23, 2, 3, 1};
    for (std::size_t i = 0; i < fields.size(); ++i) glPixelStorei(fields[i], values[i]);
    runtime.prepareResources(settings, 8, 4);
    GLint actual = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &actual); check(actual == GL_TEXTURE3, "Preparation changed active unit");
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &actual); check(static_cast<GLuint>(actual) == unpack, "Preparation changed unpack buffer");
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual); check(static_cast<GLuint>(actual) == marker2d, "Preparation changed 2d binding");
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &actual); check(static_cast<GLuint>(actual) == marker3d, "Preparation changed 3d binding");
    for (std::size_t i = 0; i < fields.size(); ++i) { glGetIntegerv(fields[i], &actual); check(actual == values[i], "Preparation changed unpack layout"); }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); glDeleteBuffers(1, &unpack);
    for (const auto field : fields) glPixelStorei(field, field == GL_UNPACK_ALIGNMENT ? 4 : 0);
    check(srgb == draw(), "Hostile unpack state changed texture upload");
    for (int unit = 0; unit < 4; ++unit) { glActiveTexture(GL_TEXTURE0 + unit); glGetIntegerv(GL_TEXTURE_BINDING_3D, &actual); check(actual == 0, "3d texture binding leaked from Pass"); }
    glActiveTexture(GL_TEXTURE0); glDeleteTextures(1, &marker2d); glDeleteTextures(1, &marker3d);
    // The published texture snapshot remains usable, while captures reject changed disk identity.
    {
        capture::InputManifest inputs; check(srgb == draw(), "Input capture changed stable image");
        check(inputs.files.size() == 2, "Prepared manifest/texture not recorded into capture");
        write(root / "palette.png", "corrupt"); rejected = false;
        try { draw(); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Capture accepted texture changed after preparation");
    }
    check(srgb == draw(), "Disk change modified live texture without publication");
    rejected = false; try { runtime.prepareResources(settings, 8, 4); } catch (const std::exception&) { rejected = true; }
    check(rejected && srgb == draw(), "Corrupt PNG rebuild lost old output");
    std::filesystem::copy_file(fixture / "palette.png", root / "palette.png", std::filesystem::copy_options::overwrite_existing);
    runtime.prepareResources(settings, 8, 4); check(srgb == draw(), "PNG recovery drifted");
    const std::string noise = R"({"id":"Tex","dimension":"3d","format":"linear-r8","extent":[32,32,32],"encoding":"hash-v1","seed":0,"mip":"none"})";
    const std::string noiseShader = "uniform sampler3D irisChannel0;uniform float irisTime;void mainImage(out vec4 c,in vec2 p){c=vec4(texelFetch(irisChannel0,ivec3(int(p.x)%32,int(p.y)%32,int(irisTime*24.0)%32),0).rrr,1);}";
    sample(noise, noiseShader, "nearest", "repeat");
    const auto firstNoise = draw(); check(firstNoise[0] == 0 && firstNoise[4] == 192, "hash-v1 byte/axis layout differs from canonical");
    std::vector<std::vector<unsigned char>> sequence;
    for (int frame = 0; frame < 24; ++frame) sequence.push_back(draw(static_cast<float>(frame) / 24));
    runtime.prepareResources(settings, 8, 4);
    for (int frame = 0; frame < 24; ++frame) check(sequence[static_cast<std::size_t>(frame)] == draw(static_cast<float>(frame) / 24), "Noise replay drifted");
    auto changedNoise = noise; changedNoise.replace(changedNoise.find("\"seed\":0"), 8, "\"seed\":1");
    check(firstNoise != sample(changedNoise, noiseShader, "nearest", "repeat"), "GPU Noise Seed has no contribution");
    // A replaced external resource commits together with a fresh feedback history.
    const std::string feedback = R"({"version":3,"id":"feedbackTexture","resourceRoot":".","entry":"mainImage","camera":"project","author":"test","license":"MIT","parameters":[],"textures":[{"id":"Data","dimension":"2d","format":"linear-r8","extent":[1,1],"encoding":"raw","path":"data.raw","mip":"none"}],"passes":[{"id":"Accum","fragment":"accum.frag","output":"linear-rgba16f","sizeDivisor":1,"channels":[{"slot":0,"source":"Data","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"},{"slot":1,"source":"Accum","frame":"previous","filter":"nearest","wrap":"clamp","mip":"none"}]},{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Accum","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"}]}]})";
    writeBytes(root / "data.raw", {64}); write(path, feedback);
    write(root / "accum.frag", "#version 330 core\nuniform sampler2D irisChannel0;uniform sampler2D irisChannel1;uniform vec2 irisResolution;void mainImage(out vec4 c,in vec2 p){c=texture(irisChannel1,p/irisResolution)+vec4(texture(irisChannel0,vec2(0.5)).r*0.5,0,0,0);}");
    write(root / "image.frag", "#version 330 core\nuniform sampler2D irisChannel0;uniform vec2 irisResolution;uniform int irisFrame;void mainImage(out vec4 c,in vec2 p){c=texture(irisChannel0,p/irisResolution);c.g=float(irisFrame)/255.0;c.a=1;}");
    runtime.prepareResources(settings, 8, 4); const auto fresh = draw(); check(fresh[0] == 32 && fresh[1] == 0, "External feedback first frame wrong");
    const auto accumulated = draw(0.05f); check(accumulated[0] == 64 && accumulated[1] == 1, "External feedback did not advance");
    write(root / "data.raw", ""); rejected = false;
    try { runtime.prepareResources(settings, 8, 4); } catch (const std::exception&) { rejected = true; }
    check(rejected && accumulated == draw(0.05f), "Bad texture retained pixels but changed feedback");
    writeBytes(root / "data.raw", {128}); runtime.prepareResources(settings, 8, 4);
    const auto reset = draw(0.05f); check(reset[0] == 64 && reset[1] == 0, "Resource commit failed to reset history");
    target.resize(31, 47, 1); check(draw(0.05f).size() == 31U * 47U * 4U, "Texture resize failed");
    check(glGetError() == GL_NO_ERROR, "Texture tests exit GL error");
    std::cout << "Texture GPU PNG/sRGB/raw2d/raw3d/XYZ/filter/wrap/Mip/budget/unpack+binding isolation/capture identity/Seed/24-frame replay/atomic recovery/history/resize PASS\n";
}
