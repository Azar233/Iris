#include "plugin/ShaderSceneRenderer.h"
#include "plugin/ShaderSceneInput.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include "scene/SceneDocument.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <limits>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::trunc) << text;
    static int revision = 0;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++revision));
}
std::vector<unsigned char> pixels(const RenderTarget& target) {
    std::vector<unsigned char> image(static_cast<std::size_t>(target.width() * target.height() * 4));
    glBindTexture(GL_TEXTURE_2D, target.colorTexture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data()); return image;
}
double percentile(std::vector<double> values,double fraction) {
    std::sort(values.begin(),values.end());
    return values[static_cast<std::size_t>(std::ceil(fraction*values.size()))-1];
}
void testPassTimings(const std::filesystem::path& shaders,unsigned int vao) {
    std::ostringstream report;
    report<<std::setprecision(12)<<"{\"gpu\":"<<std::quoted(reinterpret_cast<const char*>(glGetString(GL_RENDERER)))
        <<",\"openGl\":"<<std::quoted(reinterpret_cast<const char*>(glGetString(GL_VERSION)))<<",\"cases\":[";
    bool firstCase=true;
    for(const auto* name:{"sequence","enscape"}) {
        iris::ShaderSceneRenderer runtime(shaders);RenderTarget target;Camera camera;SceneDocument document;std::string error;
        check(loadSceneDocument(shaders.parent_path()/"assets/shader-scenes"/name/(std::string(name)+".shaderscene"),document,error),error.c_str());
        auto settings=document.renderer;settings.shaderSceneSeed=7;
        camera.setOrbitState(document.camera);
        for(const auto width:{320,640}) {
            const int height=width*9/16;target.resize(width,height,1);
            if(width==320)runtime.prepareResources(settings,width,height);
            runtime.invalidateHistory();
            std::vector<double> cpu,gpu;std::vector<std::vector<double>> passes;
            for(int frame=0;frame<96;++frame) {
                const auto start=std::chrono::steady_clock::now();
                runtime.renderFrame({target,camera,settings,width,height,static_cast<float>(frame)/24,vao});
                const auto end=std::chrono::steady_clock::now();glFinish();
                const auto info=runtime.frameInfo();const auto timings=runtime.passGpuMilliseconds();
                if(frame==0)check(!info.gpuTimeValid&&timings.empty(),"New package/resize retained stale timing sample");
                if(frame<24)continue;
                check(info.gpuTimeValid&&info.gpuTimeUpdated&&timings.size()==info.passNames.size(),"Incomplete asynchronous Pass timing");
                double sum=0;
                if(passes.empty())passes.resize(timings.size());
                for(std::size_t i=0;i<timings.size();++i){
                    check(std::isfinite(timings[i])&&timings[i]>=0,"Invalid Pass timestamp interval");
                    passes[i].push_back(timings[i]);sum+=timings[i];
                }
                check(sum>0&&sum<=info.gpuMilliseconds+1e-6,"Pass samples do not belong to enclosing frame interval");
                gpu.push_back(info.gpuMilliseconds);cpu.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            }
            check(glGetError()==GL_NO_ERROR,"Pass timing GL error");
            if(!firstCase)report<<',';
            firstCase=false;
            report<<"{\"package\":"<<std::quoted(name)<<",\"width\":"<<width<<",\"height\":"<<height
                <<",\"samples\":"<<gpu.size()<<",\"warmupFrames\":24,\"fps\":24,\"seed\":7,\"privateTextureBytes\":"<<runtime.estimatedBytes()
                <<",\"cpuP50Ms\":"<<percentile(cpu,0.5)<<",\"cpuP95Ms\":"<<percentile(cpu,0.95)
                <<",\"gpuP50Ms\":"<<percentile(gpu,0.5)<<",\"gpuP95Ms\":"<<percentile(gpu,0.95)<<",\"passes\":[";
            const auto names=runtime.frameInfo().passNames;
            for(std::size_t i=0;i<passes.size();++i){if(i)report<<',';
                report<<"{\"name\":"<<std::quoted(names[i])<<",\"p50Ms\":"<<percentile(passes[i],0.5)<<",\"p95Ms\":"<<percentile(passes[i],0.95)<<'}';}
            report<<"]}";
        }
    }
    report<<"]}\n";
    if(const char* path=std::getenv("IRIS_SHADER_SCENE_COST_REPORT")){
        std::ofstream output(std::filesystem::u8path(path),std::ios::binary);output<<report.str();output.flush();
        check(static_cast<bool>(output),"Cannot write ShaderScene cost report");
    }
    std::cout<<"ShaderScene dual-package asynchronous Pass timings / 320+640 / resize invalidation / 72 samples: PASS\n";
}
}
void testShaderSceneMultiPass(const std::filesystem::path&, const std::filesystem::path&, unsigned int);
void testShaderSceneEntry(const std::filesystem::path&, const std::filesystem::path&, unsigned int);
void testShaderSceneTexturesGpu(const std::filesystem::path&, const std::filesystem::path&, unsigned int);
void testShaderSceneReload(const std::filesystem::path&, const std::filesystem::path&, unsigned int);
void testShaderSceneHistory(const std::filesystem::path&, const std::filesystem::path&, unsigned int);
int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "ShaderScene test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    const auto directory = std::filesystem::temp_directory_path() / ("IrisShaderSceneGpu-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        check(argc == 2, "Shader directory required");
        std::filesystem::create_directories(directory / "package");
        const auto fragment = directory / "package/image.frag";
        const auto manifest = directory / "test.shaderscene";
        write(manifest, R"({"version":1,"id":"test","resourceRoot":"package","fragment":"image.frag","entry":"main","output":"display-rgba8","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.5,"min":0,"max":1}]})");
        const std::string source = "#version 330 core\nout vec4 color;\nuniform float gain;uniform float irisTime;uniform vec2 irisResolution;uniform vec3 irisCameraPosition;\nvoid main(){color=vec4(gain, gl_FragCoord.x/irisResolution.x, irisTime + irisCameraPosition.x*0.01,1);}\n";
        write(fragment, source);
        SceneDocument document; std::string error;
        check(loadSceneDocument(manifest, document, error), error.c_str());
        RendererSettings settings = document.renderer;
        Camera camera; RenderTarget target; target.resize(64, 32, 1);
        unsigned int vao = 0; glGenVertexArrays(1, &vao);
        {
            iris::ShaderSceneRenderer runtime(std::filesystem::u8path(argv[1]));
            runtime.prepareResources(settings, 64, 32);
            auto draw = [&](float time) {
                runtime.renderFrame({target, camera, settings, target.width(), target.height(), time, vao});
                check(glGetError() == GL_NO_ERROR, "ShaderScene draw GL error"); return pixels(target);
            };
            const auto first = draw(0.25f); check(first == draw(0.25f), "Fixed frame differs");
            check(first[0] >= 127 && first[0] <= 128, "Parameter not consumed by GPU");
            check(first != draw(0.5f), "Time input has no contribution");
            camera.moveLocal(0, 1); check(first != draw(0.25f), "Camera input has no contribution"); camera.reset();
            document.renderer.shaderSceneMouse={.5f,.5f,1,1};
            check(saveSceneDocument(directory / "saved.myscene", document, error), error.c_str());
            SceneDocument reopened; check(loadSceneDocument(directory / "saved.myscene", reopened, error), error.c_str());
            check(reopened.renderer.shaderSceneResource == settings.shaderSceneResource, "Relative reference round trip failed");
            check(reopened.renderer.shaderSceneMouse==glm::vec4(0),"Scene persisted live mouse input");
            write(fragment, "#version 330 core\ninvalid shader\n");
            auto report = runtime.reloadResources(settings);
            check(report.failed > 0 && report.retained > 0 && first == draw(0.25f), "Compile rejection lost old image");
            write(fragment, "#version 330 core\nout vec4 color;uniform vec3 gain;void main(){color=vec4(gain,1);}\n");
            report = runtime.reloadResources(settings, true);
            check(report.failed > 0 && first == draw(0.25f), "Uniform mismatch lost old image");
            write(directory / "outside.glsl", "// Outside the declared package root\n");
            write(fragment, "#version 330 core\n#include \"../outside.glsl\"\nout vec4 color;void main(){color=vec4(1);}\n");
            report = runtime.reloadResources(settings, true);
            check(report.failed > 0 && first == draw(0.25f), "Outside include accepted");
            write(fragment, source); report = runtime.reloadResources(settings, true);
            check(report.failed == 0 && report.reloaded > 0 && first == draw(0.25f), "Shader recovery failed");
            target.resize(31, 47, 1); const auto resized = draw(0.25f);
            check(resized.size() == 31 * 47 * 4 && resized == draw(0.25f), "Resize output differs");
            write(fragment, "#version 330 core\ninvalid again\n");
            bool rejected = false;
            try { runtime.prepareResources(settings, 31, 47); } catch (const std::exception&) { rejected = true; }
            check(rejected && resized == draw(0.25f), "Failed package rebuild lost live program");
            write(fragment, source);
            write(fragment, "#version 330 core\nout vec4 color;uniform vec2 irisResolution;void main(){if(gl_FragCoord.x < irisResolution.x*0.5)discard;color=vec4(1,0,1,1);}\n");
            runtime.prepareResources(settings, 31, 47);
            const auto partial = draw(0.25f);
            check(partial[0] == 0 && partial[1] == 0 && partial[2] == 0 && partial[3] == 255
                && partial[partial.size() - 4] == 255 && partial == draw(0.25f), "Discard retained undeclared previous frame pixels");
            write(fragment,"#version 330 core\nout vec4 color;uniform vec4 irisMouse;uniform vec2 irisViewportResolution;void main(){color=vec4(irisMouse.xy/irisViewportResolution,irisMouse.z/7.,irisMouse.w);}\n");
            runtime.prepareResources(settings,31,47);
            const auto zeroMouse=draw(.25f);
            check(zeroMouse[0]==0 && zeroMouse[1]==0 && zeroMouse[2]==0 && zeroMouse[3]==0,"Mouse default is not zero");
            settings.shaderSceneMouse=iris::shaderSceneMouseInput(90,30,10,10,320,80,7,true);
            const auto input=draw(.375f);
            check(input[0]>=63 && input[0]<=64 && input[1]>=191 && input[1]<=192 && input[2]==255 && input[3]==255,
                "Viewport mouse coordinate / button / inside GPU contribution failed");
            check(!runtime.runtimeStatus().historyValid && input==draw(.375f),"Changed mouse did not reset / paused mouse drifted");
            draw(.5f);check(runtime.runtimeStatus().historyValid,"Unchanged mouse reset history");
            auto invalid=settings;invalid.shaderSceneMouse.z=.5f;
            rejected=false;try{runtime.renderFrame({target,camera,invalid,31,47,.5f,vao});}catch(const std::exception&){rejected=true;}
            check(rejected && input==draw(.5f),"Invalid mouse mutated output/history");
            invalid.shaderSceneMouse.x=std::numeric_limits<float>::quiet_NaN();
            rejected=false;try{runtime.renderFrame({target,camera,invalid,31,47,.5f,vao});}catch(const std::exception&){rejected=true;}
            check(rejected && runtime.runtimeStatus().frame==1,"Nonfinite mouse accepted");
            write(fragment,"#version 330 core\nout vec4 color;uniform vec3 irisMouse;void main(){color=vec4(irisMouse,1);}\n");
            report=runtime.reloadResources(settings,true);
            check(report.failed==1 && input==draw(.5f),"Mouse type rejection lost prior program");
            settings.shaderSceneMouse=iris::shaderSceneMouseInput(10,10,10,10,320,80,0,false);
            check(zeroMouse==draw(.625f) && !runtime.runtimeStatus().historyValid,"UI capture did not clear mouse/history");
            check(iris::shaderSceneMouseInput(330,20,10,10,320,80,1,true)==glm::vec4(0),"Outside right edge accepted");
            check(iris::shaderSceneMouseInput(20,90,10,10,320,80,1,true)==glm::vec4(0),"Outside bottom edge accepted");
            std::cout<<"Mouse GPU: zero/coordinates/buttons/inside/paused/history/type rejection/nonfinite/retention PASS\n";
        }
        testShaderSceneMultiPass(std::filesystem::u8path(argv[1]), directory, vao);
        testShaderSceneEntry(std::filesystem::u8path(argv[1]), directory, vao);
        testShaderSceneTexturesGpu(std::filesystem::u8path(argv[1]), directory, vao);
        testShaderSceneReload(std::filesystem::u8path(argv[1]), directory, vao);
        testShaderSceneHistory(std::filesystem::u8path(argv[1]), directory, vao);
        {
            const auto shaderRoot=std::filesystem::u8path(argv[1]);
            iris::ShaderSceneRenderer runtime(shaderRoot);RendererSettings settings;
            settings.shaderSceneResource=(shaderRoot.parent_path()/"assets/shader-scenes/mouse/mouse.shaderscene").u8string();
            settings.shaderSceneMouse={.25f,.75f,7,1};Camera camera;RenderTarget target;target.resize(64,32,1);
            runtime.prepareResources(settings,64,32);runtime.renderFrame({target,camera,settings,64,32,0,vao});
            const auto image=pixels(target);
            check(image[0]>=37 && image[0]<=38 && image[1]>=87 && image[1]<=88 && image[2]>=119 && image[2]<=120,
                "Half-size mainImage Pass did not receive final viewport mouse coordinates");
            std::cout<<"Mouse half-size mainImage / current+previous Pass viewport coordinates: PASS\n";
        }
        testPassTimings(std::filesystem::u8path(argv[1]),vao);
        glDeleteVertexArrays(1, &vao);
        check(glGetError() == GL_NO_ERROR, "Exit GL error");
        std::cout << "ShaderScene real GL: parameters/time/camera, fixed repeat, Scene reference, compile/type/include failure retention, recovery, resize, discard clear and destruction PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(directory);
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
