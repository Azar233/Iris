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
        // Borrowed half-resolution cloud/ray inputs must drive the actual
        // compositor rather than bypassing the common binding table.
        unsigned int cloudTextures[3]{}; glGenTextures(3,cloudTextures);
        const auto upload=[&](unsigned int texture,GLint internal,GLenum format,int channels,float red,float green,float blue,float alpha) {
            std::vector<float> data(static_cast<std::size_t>(160*90*channels));
            const float values[4]={red,green,blue,alpha};
            for(std::size_t i=0;i<data.size();++i) data[i]=values[i%channels];
            glBindTexture(GL_TEXTURE_2D,texture); glTexImage2D(GL_TEXTURE_2D,0,internal,160,90,0,format,GL_FLOAT,data.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        };
        upload(cloudTextures[0],GL_RGBA16F,GL_RGBA,4,.4f,.8f,.1f,.2f);
        upload(cloudTextures[1],GL_RG32F,GL_RG,2,0,0,0,0);
        upload(cloudTextures[2],GL_R16F,GL_RED,1,.3f,0,0,0);
        post.cloudEnabled=true; post.cloudTexture=cloudTextures[0]; post.cloudDepthTexture=cloudTextures[1];
        post.cloudWidth=160;post.cloudHeight=90;render();const auto cloud=pixels(target);
        require(cloud!=reference,"Plugin ignored borrowed cloud input");
        auto missingCloud=iris::RenderPluginFrame(target,camera,settings,320,180,1.25f,0,&post);
        for(auto& binding:missingCloud.textures) if(binding.name=="cloudDepth") binding.texture=0;
        rejects([&]{plugin->renderFrame(missingCloud);});require(pixels(target)==cloud,"Cloud rejection changed old image");
        auto wrongCloud=iris::RenderPluginFrame(target,camera,settings,320,180,1.25f,0,&post);
        for(auto& binding:wrongCloud.textures) if(binding.name=="cloud") binding.width=159;
        rejects([&]{plugin->renderFrame(wrongCloud);});
        post.cloudEnabled=false;post.cloudTexture=post.cloudDepthTexture=0;post.cloudWidth=post.cloudHeight=0;
        // Ray scattering is visible only on sky depth.
        target.bindOpaqueScene();glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);target.resolveOpaqueScene();
        render();const auto sky=pixels(target);
        post.godRaysEnabled=true;post.godRaysTexture=cloudTextures[2];post.godRaysWidth=160;post.godRaysHeight=90;
        render();require(pixels(target)!=sky,"Plugin ignored borrowed ray input");
        auto badRay=iris::RenderPluginFrame(target,camera,settings,320,180,1.25f,0,&post);
        for(auto& binding:badRay.textures) if(binding.name=="rays") binding.format=iris::TextureFormat::HdrColor;
        const auto rayPixels=pixels(target);rejects([&]{plugin->renderFrame(badRay);});
        require(pixels(target)==rayPixels,"Ray rejection changed old output");
        post.godRaysEnabled=false;post.godRaysTexture=0;post.godRaysWidth=post.godRaysHeight=0;
        glDeleteTextures(3,cloudTextures);initialize(target,320,180);post.depthTexture=target.sceneDepthTexture();
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
