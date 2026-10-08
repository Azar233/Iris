#include "plugin/RenderPluginRegistry.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/RenderTarget.h"
#include "render/PostProcessSettings.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class Function> void rejects(Function fn){bool failed=false;try{fn();}catch(const std::invalid_argument&){failed=true;}require(failed,"Invalid GPU plugin request accepted");}
std::vector<unsigned char> pixels(RenderTarget& target){
    target.bindFinal();std::vector<unsigned char> result(static_cast<std::size_t>(target.width()*target.height()*4));
    glReadPixels(0,0,target.width(),target.height(),GL_RGBA,GL_UNSIGNED_BYTE,result.data());return result;
}
void initialize(RenderTarget& target,int width,int height){
    target.resize(width,height,1);target.bindOpaqueScene();glClearColor(.7f,.2f,.1f,1);
    glClearDepth(.5);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);target.resolveOpaqueScene();
    target.bindHdrSceneForOverlay();glClearColor(2.0f,.2f,.1f,1);glClear(GL_COLOR_BUFFER_BIT);
}
}
int main(){
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(64,64,"Postprocess plugin contract",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGL(glfwGetProcAddress)){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int exitCode=0;
    try{
        RenderTarget target;Camera camera;RendererSettings settings;PostProcessSettings post;
        initialize(target,320,180);post.depthTexture=target.sceneDepthTexture();post.bloom=false;
        auto plugin=iris::builtinRenderPlugins().create(iris::postProcessPluginId,
            {iris::openGlFullscreenService},std::filesystem::path(MYRENDERER_SOURCE_DIR)/"shaders");
        const auto render=[&]{plugin->renderFrame({target,camera,settings,target.width(),target.height(),1.25f,0,&post});};
        render();const auto reference=pixels(target);
        require(std::any_of(reference.begin(),reference.end(),[](unsigned char value){return value>0&&value<255;}),"Postprocess produced no color");
        auto bad=iris::RenderPluginFrame(target,camera,settings,320,180,1.25f,0,&post);
        bad.textures[1].texture=0;
        rejects([&]{plugin->renderFrame(bad);});require(pixels(target)==reference,"Rejected binding changed output");
        rejects([&]{plugin->renderFrame({target,camera,settings,321,180,1.25f,0,&post});});
        require(pixels(target)==reference,"Rejected size changed output");
        rejects([&]{plugin->renderFrame({target,camera,settings,320,180,1.25f,0});});
        {
            RenderTarget alternate;
            initialize(alternate,320,180);
            alternate.bindHdrSceneForOverlay();glClearColor(.2f,2.0f,.1f,1);glClear(GL_COLOR_BUFFER_BIT);
            auto custom=iris::RenderPluginFrame(target,camera,settings,320,180,1.25f,0,&post);
            custom.textures[1].texture=alternate.hdrColorTexture();
            plugin->renderFrame(custom);
            require(pixels(target)!=reference,"Plugin ignored borrowed HDR binding");
        }
        post.exposure=.25f;render();require(pixels(target)!=reference,"Parameter bridge ignored exposure");
        post.exposure=1;post.temporalAa=true;
        for(int frame=0;frame<64;++frame) { render(); }
        const auto temporal=pixels(target);
        plugin->invalidateHistory();for(int frame=0;frame<64;++frame)render();
        require(pixels(target)==temporal,"History reset changed fixed input");
        initialize(target,160,90);post.depthTexture=target.sceneDepthTexture();render();
        initialize(target,320,180);post.depthTexture=target.sceneDepthTexture();
        for(int frame=0;frame<64;++frame) { render(); }
        require(pixels(target)==temporal,"Resize restoration changed output");
        require(plugin->estimatedBytes()>0,"Plugin memory report missing");
        plugin.reset();require(glGetError()==GL_NO_ERROR,"Postprocess teardown GL error");
        plugin=iris::builtinRenderPlugins().create(iris::postProcessPluginId,{iris::openGlFullscreenService},std::filesystem::path(MYRENDERER_SOURCE_DIR)/"shaders");
        for(int frame=0;frame<64;++frame) { render(); }
        require(pixels(target)==temporal,"Plugin recreation changed output");
        require(glGetError()==GL_NO_ERROR,"Postprocess plugin GL error");
        std::cout<<"Postprocess bindings/rejection/parameters/history/resize/recreation: PASS\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';exitCode=1;}
    glfwDestroyWindow(window);glfwTerminate();return exitCode;
}
