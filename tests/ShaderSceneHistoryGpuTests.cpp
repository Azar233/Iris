#include "plugin/ShaderSceneRenderer.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cmath>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool encoded(unsigned char value,float linear){return value>=std::floor(linear*255.f) && value<=std::ceil(linear*255.f);}
}
void testShaderSceneHistory(const std::filesystem::path& shaders,const std::filesystem::path& parent,unsigned int vao) {
    const auto root=parent/"history";std::filesystem::create_directories(root);
    const auto manifest=root/"history.shaderscene";
    std::ofstream(manifest) << R"({"version":2,"id":"history","resourceRoot":".","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.125,"min":0,"max":1}],"passes":[{"id":"Accum","fragment":"accum.frag","output":"linear-rgba16f","sizeDivisor":1,"channels":[{"slot":0,"source":"Accum","frame":"previous","filter":"nearest","wrap":"clamp","mip":"none"}]},{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Accum","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"}]}]})";
    const std::string accumulation="#version 330 core\nuniform sampler2D irisChannel0;uniform vec2 irisResolution;uniform float gain;uniform int irisFrame;uniform bool irisHistoryValid;out vec4 color;void main(){color=vec4(texture(irisChannel0,gl_FragCoord.xy/irisResolution).r+gain,float(irisFrame)/16.,irisHistoryValid?1.:0.,1.);}\n";
    std::ofstream(root/"accum.frag")<<accumulation;
    std::ofstream(root/"image.frag")<<"#version 330 core\nuniform sampler2D irisChannel0;uniform vec2 irisResolution;out vec4 color;void main(){color=texture(irisChannel0,gl_FragCoord.xy/irisResolution);}\n";
    RendererSettings settings;settings.shaderSceneResource=manifest.u8string();
    Camera camera;RenderTarget target;target.resize(16,8,1);
    iris::ShaderSceneRenderer runtime(shaders);runtime.prepareResources(settings,16,8);
    const auto draw=[&](float time){
        runtime.renderFrame({target,camera,settings,target.width(),target.height(),time,vao});
        std::vector<unsigned char> pixels(static_cast<std::size_t>(target.width()*target.height()*4));
        glBindTexture(GL_TEXTURE_2D,target.colorTexture());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        check(glGetError()==GL_NO_ERROR,"History GPU error");return pixels;
    };
    const auto gain=[&](float value){ModuleParameterValue parameter;parameter.number=value;settings.shaderSceneParameters={{"gain",parameter}};};
    check(draw(0)[0]==32 && draw(.125f)[0]==64,"Default history did not advance");
    gain(.25f);check(draw(.25f)[0]==64 && !runtime.runtimeStatus().historyValid,"Default parameter change did not reset");
    settings.shaderSceneHistory={false,false,.5f};
    check(draw(.25f)[0]==64 && runtime.runtimeStatus().frame==0,"Policy change did not reset");
    const auto advanced=draw(.375f);
    check(encoded(advanced[0],.5f) && runtime.runtimeStatus().frame==1,"Permissive policy did not advance");
    gain(.125f);check(encoded(draw(.5f)[0],.625f) && runtime.runtimeStatus().historyValid,"Parameter preservation failed");
    camera.moveLocal(0,1);const auto moved=draw(.625f);
    check(encoded(moved[0],.75f) && runtime.runtimeStatus().frame==3 && runtime.runtimeStatus().historyValid,"Camera preservation failed");
    camera.moveLocal(0,1);check(moved==draw(.625f) && runtime.runtimeStatus().frame==3,"Paused redraw advanced preserved feedback");
    gain(.25f);const auto pausedParameter=draw(.625f);
    check(encoded(pausedParameter[0],.875f) && pausedParameter==draw(.625f) && runtime.runtimeStatus().frame==3,"Paused parameter edit consumed its own current output");
    gain(.125f);check(moved==draw(.625f),"Paused parameter restore changed the immutable previous input");
    check(encoded(draw(1.125f)[0],.875f) && runtime.runtimeStatus().frame==4,"Inclusive maximum time step reset incorrectly");
    check(draw(1.75f)[0]==32 && !runtime.runtimeStatus().historyValid,"Excess time step retained history");
    check(draw(1.625f)[0]==32 && runtime.runtimeStatus().frame==0,"Rewind retained history under permissive policy");
    settings.shaderSceneHistory.resetOnCamera=true;draw(1.625f);draw(1.75f);camera.moveLocal(0,1);
    check(draw(1.875f)[0]==32 && !runtime.runtimeStatus().historyValid,"Camera reset option ignored");
    settings.shaderSceneHistory.resetOnParameters=true;draw(1.875f);draw(2.f);gain(.25f);
    check(draw(2.125f)[0]==64 && !runtime.runtimeStatus().historyValid,"Parameter reset option ignored");
    settings.shaderSceneHistory={false,false,.5f};draw(2.125f);const auto retained=draw(2.25f);
    auto bad=settings;bad.shaderSceneHistory.maximumTimeStep=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;
    try {runtime.renderFrame({target,camera,bad,16,8,2.25f,vao});}catch(const std::exception&){rejected=true;}
    check(rejected && runtime.runtimeStatus().frame==1 && retained==draw(2.25f),"Invalid runtime policy mutated history/output");
    target.resize(31,47,1);check(draw(2.25f)[0]==64 && !runtime.runtimeStatus().historyValid,"Permissive resize kept invalid buffers");
    draw(2.375f);runtime.invalidateHistory();check(draw(2.375f)[0]==64 && runtime.runtimeStatus().frame==0,"Explicit clear ignored");
    const auto replay=[&](){runtime.invalidateHistory();std::vector<std::vector<unsigned char>> frames;
        for(int frame=0;frame<24;++frame) {frames.push_back(draw(static_cast<float>(frame)/24));}
        return frames;
    };
    check(replay()==replay(),"Configured history sequence not reproducible");
    const auto before=draw(23.f/24);std::ofstream(root/"accum.frag")<<"invalid source\n";
    check(runtime.reloadResources(settings).failed==1 && before==draw(23.f/24),"Failed reload lost configured feedback");
    std::ofstream(root/"accum.frag")<<accumulation;
    check(runtime.reloadResources(settings,true).reloaded==2 && runtime.runtimeStatus().resetPending,"Success reload retained stale history");
    check(draw(23.f/24)[0]==64 && !runtime.runtimeStatus().historyValid,"Reload did not keep policy and clear buffers");
    std::cout<<"History policy GPU: defaults/preserve parameters+camera/pause/bounded step/rewind/policy change/invalid/resize/clear/reload/24-frame replay PASS\n";
}
