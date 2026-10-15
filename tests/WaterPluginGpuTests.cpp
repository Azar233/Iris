#include "plugin/RenderPluginRegistry.h"
#include "render/Renderer.h"
#include "WaterRenderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <array>
#include <fstream>
#include <chrono>
#include <vector>
#include <cmath>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message = "Invalid water transaction accepted") {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
struct Grid { GLuint vao, vertices, indices; };
Grid inspect(const iris::IndexedTriangleGeometry& geometry, int resolution) {
    iris::validateIndexedTriangleGeometry(geometry);
    require(glIsVertexArray(geometry.vertexArray), "Missing grid VAO");
    require(geometry.indexCount == static_cast<std::size_t>(resolution * resolution * 6), "Wrong index count");
    GLint previous = 0, vertices = 0, indices = 0, size = 0, enabled = 0, components = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
    glBindVertexArray(geometry.vertexArray);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vertices);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &components);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &indices);
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    require(enabled && components == 2 && vertices && indices, "Invalid grid attributes");
    require(size == resolution * resolution * 6 * 4, "Wrong index storage");
    GLint array = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
    glBindBuffer(GL_ARRAY_BUFFER, vertices);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    require(size == (resolution + 1) * (resolution + 1) * 2 * 4, "Wrong vertex storage");
    glBindBuffer(GL_ARRAY_BUFFER, array);
    glBindVertexArray(previous);
    return {geometry.vertexArray, static_cast<GLuint>(vertices), static_cast<GLuint>(indices)};
}
void alive(const Grid& grid, bool expected) {
    require(bool(glIsVertexArray(grid.vao)) == expected && bool(glIsBuffer(grid.vertices)) == expected
        && bool(glIsBuffer(grid.indices)) == expected, "Grid lifetime mismatch");
}
std::size_t bytes(int resolution) {
    return static_cast<std::size_t>((resolution + 1) * (resolution + 1) * 8 + resolution * resolution * 24);
}
std::array<GLuint,2> programs(const iris::RenderPlugin& plugin) {
    GLint previous = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    std::array<GLuint,2> result{};
    for (int path = 0; path < 2; ++path) {
        auto* shader = plugin.borrowShadingShader(path != 0);
        require(shader != nullptr, "Missing owned water shader"); shader->use();
        GLint program = 0, linked = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        require(linked == GL_TRUE, "Unlinked owned water program");
        result[path] = static_cast<GLuint>(program);
    }
    glUseProgram(previous); return result;
}
void alive(const std::array<GLuint,2>& values, bool expected) {
    require(bool(glIsProgram(values[0])) == expected && bool(glIsProgram(values[1])) == expected,
        "Water program lifetime mismatch");
}
void checkFrameUniforms(GLuint program, const iris::WaterFrameInputs& input,
    const Camera& camera, float time, bool forward) {
    const auto check = [&](const char* name, const float* expected, int count, bool required) {
        const GLint location=glGetUniformLocation(program,name);
        require(location>=0 || !required,"Expected frame uniform optimized out");
        if(location<0) return;
        std::array<float,16> actual{}; glGetUniformfv(program,location,actual.data());
        for(int i=0;i<count;++i) require(std::abs(actual[i]-expected[i])<1e-6f,"Borrowed frame uniform mismatch");
    };
    const auto position=camera.position();
    check("uCurrentViewProjection",&input.currentViewProjection[0][0],16,true);
    check("uPreviousViewProjection",&input.previousViewProjection[0][0],16,!forward);
    check("uCameraPosition",&position[0],3,true);
    check("uTime",&time,1,true);
    check("uPreviousTime",&input.previousTimeSeconds,1,!forward);
    check("uCameraSurfaceHeight",&input.cameraSurfaceHeight,1,forward);
    const GLint location=glGetUniformLocation(program,"uMotionHistoryValid");
    require(location>=0 || forward,"Motion flag missing from motion shader");
    if(location>=0) {
        GLint actual=0; glGetUniformiv(program,location,&actual);
        require(bool(actual)==input.motionHistoryValid,"Motion validity uniform mismatch");
    }
}
// Use the real water vertex/G-buffer programs and geometry. Restrict outputs
// exactly as the backend's Deferred+TAA motion subpass does.
void checkDraw(iris::RenderPlugin& plugin, RendererSettings settings) {
    GLuint fbo = 0, depth = 0, query = 0;
    std::array<GLuint,4> textures{};
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(4, textures.data());
    const std::array<GLenum,4> all{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,64,64,0,GL_RGBA,GL_FLOAT,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,all[i],GL_TEXTURE_2D,textures[i],0);
    }
    glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,64,64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Draw fixture FBO incomplete");
    glGenQueries(1,&query);
    Camera camera; camera.setOrbitPose({0,0,0},45,45,6,45);
    RenderTarget target;
    settings.water.level=0; settings.water.extent=4;
    auto* shader=plugin.borrowShadingShader(true); shader->use();
    GLint program=0; glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    auto projection=camera.projectionMatrix(1);
    projection[2][0]+=0.0123f; projection[2][1]-=0.007f;
    const auto current=projection*camera.viewMatrix();
    auto previous=current; previous[3][0]+=0.1f;
    settings.water.timeSeconds=0.125f;
    iris::WaterFrameInputs input{current,previous,0.0625f,0.375f,true};
    // Both owned programs receive the resolved packet. Generic frame time is
    // deliberately different: water must keep its existing settings time.
    for(const bool forward : {true,false}) {
        auto* owned=plugin.borrowShadingShader(!forward); owned->use();
        GLint id=0; glGetIntegerv(GL_CURRENT_PROGRAM,&id);
        plugin.renderFrame({target,camera,settings,64,64,99,0,nullptr,owned,&input});
        checkFrameUniforms(id,input,camera,settings.water.timeSeconds,forward);
        auto legacy=settings; legacy.water.timeSeconds=9;
        plugin.renderFrame({target,camera,legacy,64,64,99,0,nullptr,owned});
        checkFrameUniforms(id,input,camera,settings.water.timeSeconds,forward);
    }
    shader->use();
    const std::array<GLenum,4> motion{GL_NONE,GL_NONE,GL_NONE,GL_COLOR_ATTACHMENT3};
    const std::array<float,4> clear{0.25f,0.5f,0.75f,0.125f};
    const std::array<GLenum,13> keys{GL_CURRENT_PROGRAM,GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,
        GL_ACTIVE_TEXTURE,GL_TEXTURE_BINDING_2D,GL_ARRAY_BUFFER_BINDING,GL_DEPTH_WRITEMASK,GL_DEPTH_FUNC,
        GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_DRAW_BUFFER0,GL_DRAW_BUFFER1,GL_DRAW_BUFFER3};
    for (const auto quality : {WaterQuality::High,WaterQuality::Low}) {
        settings.water.quality=quality;
        for (const bool motionOnly : {false,true}) for(int history=0;history<3;++history) {
            glDrawBuffers(4,all.data()); glDepthMask(GL_TRUE);
            for (int i=0;i<4;++i) glClearBufferfv(GL_COLOR,i,clear.data());
            glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
            glDrawBuffers(4,motionOnly?motion.data():all.data());
            glViewport(0,0,64,64); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
            glDepthMask(motionOnly?GL_FALSE:GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            input.motionHistoryValid=history!=1;
            input.previousViewProjection=history==2?current:previous;
            input.previousTimeSeconds=history==2?settings.water.timeSeconds:0.0625f;
            plugin.renderFrame({target,camera,settings,64,64,99,0,nullptr,shader,&input});
            checkFrameUniforms(program,input,camera,settings.water.timeSeconds,false);
            std::array<GLint,13> before{},after{};
            std::array<GLint,2> polygonBefore{-1,-1};
            glGetIntegerv(GL_POLYGON_MODE,polygonBefore.data());
            for (std::size_t i=0;i<keys.size();++i) glGetIntegerv(keys[i],&before[i]);
            glBeginQuery(GL_PRIMITIVES_GENERATED,query);
            require(plugin.drawIndexedGeometry(settings)==1,"Wrong successful draw count");
            glEndQuery(GL_PRIMITIVES_GENERATED);
            GLuint generated=0; glGetQueryObjectuiv(query,GL_QUERY_RESULT,&generated);
            const auto resolution=quality==WaterQuality::Low?water::lowGridResolution:water::gridResolution;
            require(generated==static_cast<GLuint>(resolution*resolution*2),"Draw selected wrong grid or did not execute");
            for (std::size_t i=0;i<keys.size();++i) glGetIntegerv(keys[i],&after[i]);
            require(before==after && after[0]==program,"Draw changed external bindings or state");
            GLint vao=-1; std::array<GLint,4> viewport{}; std::array<GLint,2> polygon{-1,-1};
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_VIEWPORT,viewport.data());
            glGetIntegerv(GL_POLYGON_MODE,polygon.data());
            require(vao==0 && viewport==std::array<GLint,4>{0,0,64,64},"Draw changed viewport or left VAO bound");
            require(glIsEnabled(GL_DEPTH_TEST),"Draw disabled depth test");
            require(!glIsEnabled(GL_BLEND) && !glIsEnabled(GL_CULL_FACE),"Draw enabled blend/cull");
            require(polygonBefore[0]==GL_FILL && polygon==polygonBefore,"Draw changed polygon mode");
            std::vector<float> pixels(64*64*4);
            glReadBuffer(GL_COLOR_ATTACHMENT3); glReadPixels(0,0,64,64,GL_RGBA,GL_FLOAT,pixels.data());
            std::size_t visible=0, moved=0;
            for (std::size_t i=0;i<pixels.size();i+=4) {
                if(pixels[i+2]==1) { ++visible; if(std::abs(pixels[i])>0.0001f || std::abs(pixels[i+1])>0.0001f) ++moved; }
                if(history==1 && pixels[i+2]!=clear[2])
                    require(pixels[i]==0 && pixels[i+1]==0 && pixels[i+2]==0,"Reset motion was not zero");
                if(history==2 && pixels[i+2]==1)
                    require(std::abs(pixels[i])<1e-6f && std::abs(pixels[i+1])<1e-6f,"Equal frames produced motion");
            }
            if(history==1) require(visible==0,"Reset retained valid object motion");
            else require(visible>64 && (history!=0 || moved>64),"Water draw produced no meaningful motion pixels");
            if (motionOnly) {
                for (int attachment=0;attachment<3;++attachment) {
                    glReadBuffer(all[attachment]); glReadPixels(0,0,64,64,GL_RGBA,GL_FLOAT,pixels.data());
                    for (std::size_t i=0;i<pixels.size();++i)
                        require(pixels[i]==clear[i%4],"Motion-only draw overwrote material attachment");
                }
                std::vector<float> depths(64*64);
                glReadPixels(0,0,64,64,GL_DEPTH_COMPONENT,GL_FLOAT,depths.data());
                for (const auto value : depths) require(value==1,"Motion-only draw overwrote depth");
            }
            require(glGetError()==GL_NO_ERROR,"Indexed surface draw GL error");
        }
    }
    glDepthMask(GL_TRUE); glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteQueries(1,&query); glDeleteRenderbuffers(1,&depth);
    glDeleteTextures(4,textures.data()); glDeleteFramebuffers(1,&fbo);
}
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "Water grid ownership", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    try {
        const auto source = std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders";
        const auto directory = std::filesystem::path(IRIS_WATER_TEST_DIRECTORY);
        std::filesystem::create_directories(directory);
        for (const auto* file : {"water.vert", "water_forward.frag", "water_gbuffer.frag", "cloud_shadow_sample.glsl", "ocean_snoise.glsl"})
            std::filesystem::copy_file(source / file, directory / file, std::filesystem::copy_options::overwrite_existing);
        Shader sentinelShader(source / "water.vert", source / "water_forward.frag"); sentinelShader.use();
        GLint sentinelProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &sentinelProgram);
        GLuint sentinelVao = 0, sentinelBuffer = 0;
        glGenVertexArrays(1, &sentinelVao); glGenBuffers(1, &sentinelBuffer);
        glBindVertexArray(sentinelVao); glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
        auto bindings = [&] {
            GLint vao = 0, array = 0, program = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            require(static_cast<GLuint>(vao) == sentinelVao && static_cast<GLuint>(array) == sentinelBuffer,
                "Preparation changed borrowed bindings");
            require(program == sentinelProgram, "Preparation changed borrowed program");
            require(glGetError() == GL_NO_ERROR, "Water transaction GL error");
        };
        iris::RenderPluginRegistry registry;
        iris::WaterRenderer* candidate = nullptr;
        int factoryCalls = 0;
        registry.add({iris::waterPluginId, iris::renderPluginApiVersion,
            {iris::openGlShadingService, iris::openGlIndexedGeometryService, iris::openGlShaderBorrowService,iris::openGlIndexedDrawService}, "MIT",
            {iris::PluginStage::Surface, {}, {}}, iris::builtinRenderPluginParameterSchema(iris::waterPluginId)},
            [&](const std::filesystem::path& shaders) {
                ++factoryCalls;
                auto plugin = std::make_unique<iris::WaterRenderer>(shaders); candidate = plugin.get(); return plugin;
            });
        RendererSettings settings; settings.water.quality = WaterQuality::High;
        iris::WaterRenderer unprepared(directory);
        rejects([&] { unprepared.RenderPlugin::drawIndexedGeometry(settings); }, "Unsupported base draw accepted");
        rejects([&] { unprepared.drawIndexedGeometry(settings); }, "Unprepared water draw accepted");
        const std::vector<std::string> services{iris::openGlShadingService, iris::openGlIndexedGeometryService, iris::openGlShaderBorrowService,iris::openGlIndexedDrawService};
        auto prepare = [&](const RendererSettings& value, const std::function<bool()>& current = {}) {
            return registry.prepareReplacement(iris::waterPluginId, services, directory, value, 64, 64, current);
        };
        rejects([&] { registry.prepareReplacement(iris::waterPluginId, {iris::openGlShadingService}, {}, settings, 64, 64); });
        rejects([&] { registry.prepareReplacement(iris::waterPluginId,
            {iris::openGlShadingService, iris::openGlIndexedGeometryService}, directory, settings, 64, 64); });
        rejects([&] { registry.prepareReplacement(iris::waterPluginId,
            {iris::openGlShadingService, iris::openGlIndexedGeometryService, iris::openGlShaderBorrowService}, directory, settings, 64, 64); });
        require(factoryCalls == 0, "Missing service allocated water resources"); bindings();
        auto owner = prepare(settings); bindings();
        auto ownedPrograms = programs(*owner); bindings();
        checkDraw(*owner,settings);
        sentinelShader.use(); glBindVertexArray(sentinelVao); glBindBuffer(GL_ARRAY_BUFFER,sentinelBuffer);
        bindings();
        Camera camera; RenderTarget target;
        const iris::RenderPluginFrame foreignShaderFrame{target, camera, settings, 64, 64, 0, 0, nullptr, &sentinelShader};
        rejects([&] { owner->renderFrame(foreignShaderFrame); }, "Foreign shader accepted by water uniforms");
        bindings();
        auto high = inspect(owner->borrowIndexedGeometry(settings), water::gridResolution);
        settings.water.quality = WaterQuality::Low;
        auto low = inspect(owner->borrowIndexedGeometry(settings), water::lowGridResolution);
        require(owner->estimatedBytes() == bytes(water::gridResolution) + bytes(water::lowGridResolution), "Wrong owned memory");
        owner->prepareResources(settings, 128, 128); bindings();
        require(owner->borrowIndexedGeometry(settings).vertexArray == low.vao, "Resize recreated fixed grid");
        require(programs(*owner) == ownedPrograms, "Resize recreated water programs");
        auto invalid = settings; invalid.water.amplitude = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { prepare(invalid); }); bindings(); alive(high, true); alive(low, true);
        const auto fragment = directory / "water_gbuffer.frag";
        const auto fragmentTime = std::filesystem::last_write_time(fragment);
        const auto vertexTime = std::filesystem::last_write_time(directory / "water.vert");
        std::ofstream(fragment) << "#version 330 core\n#error intentional second-program compile failure\n";
        // MinGW file times can be coarser than MSVC's. Make source changes
        // observable without a wall-clock sleep or compiler-specific timing.
        std::filesystem::last_write_time(fragment, fragmentTime + std::chrono::seconds(2));
        rejects([&] { prepare(settings); }, "Second water program compile failure accepted"); bindings(); alive(ownedPrograms, true);
        std::ofstream(directory / "water.vert", std::ios::app) << "\n// force both water programs into the reload transaction\n";
        std::filesystem::last_write_time(directory / "water.vert", vertexTime + std::chrono::seconds(2));
        const auto failedReload = Shader::reloadChangedShaders();
        require(failedReload.failed != 0, "Shader source change was not detected");
        require(programs(*owner) == ownedPrograms, "Failed reload replaced old water programs");
        bindings(); alive(ownedPrograms, true);
        std::filesystem::copy_file(source / "water_gbuffer.frag", fragment, std::filesystem::copy_options::overwrite_existing);
        const auto recovered = Shader::reloadChangedShaders(true);
        require(recovered.failed == 0 && recovered.reloaded == 2, "Owned water programs did not recover in atomic reload");
        const auto recoveredPrograms = programs(*owner);
        alive(ownedPrograms, false); ownedPrograms = recoveredPrograms; bindings();
        rejects([&] { registry.prepareReplacement(iris::waterPluginId, services, directory / "missing", settings, 64, 64); });
        bindings(); alive(ownedPrograms, true); alive(high, true); alive(low, true);
        Grid canceledHigh{}, canceledLow{}; int checks = 0;
        std::array<GLuint,2> canceledPrograms{};
        rejects([&] { prepare(settings, [&] {
            if (++checks == 1) return true;
            auto quality = settings; quality.water.quality = WaterQuality::High;
            canceledHigh = inspect(candidate->borrowIndexedGeometry(quality), water::gridResolution);
            canceledLow = inspect(candidate->borrowIndexedGeometry(settings), water::lowGridResolution);
            canceledPrograms = programs(*candidate);
            return false;
        }); });
        require(checks == 2, "Cancellation did not reach prepared candidate");
        bindings(); alive(canceledHigh, false); alive(canceledLow, false); alive(high, true); alive(low, true);
        alive(canceledPrograms, false); alive(ownedPrograms, true);
        auto replacement = prepare(settings); bindings();
        const auto replacementPrograms = programs(*replacement);
        auto replacementLow = inspect(replacement->borrowIndexedGeometry(settings), water::lowGridResolution);
        auto quality = settings; quality.water.quality = WaterQuality::High;
        auto replacementHigh = inspect(replacement->borrowIndexedGeometry(quality), water::gridResolution);
        owner = std::move(replacement);
        alive(ownedPrograms, false); alive(replacementPrograms, true);
        alive(high, false); alive(low, false); alive(replacementHigh, true); alive(replacementLow, true);
        owner.reset(); alive(replacementHigh, false); alive(replacementLow, false); bindings();
        alive(replacementPrograms, false);
        auto restarted = prepare(settings); bindings();
        const auto restartedPrograms = programs(*restarted);
        auto restartedLow = inspect(restarted->borrowIndexedGeometry(settings), water::lowGridResolution);
        restarted.reset(); alive(restartedLow, false);
        alive(restartedPrograms, false);
        glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &sentinelBuffer); glDeleteVertexArrays(1, &sentinelVao);
        std::cout << "Water grid/program ownership, compile failure, atomic reload, replacement, cancellation and bindings: PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
