#include "optics/CloudNoiseVolume.h"
#include "optics/CloudLightingLut.h"
#include "optics/CloudFieldCpp.h"
#include "render/CloudNoiseTexture.h"
#include "render/Shader.h"
#include "render/RenderPassSequence.h"
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
glm::vec3 position(int x, int y, int width, int height, int period, int resolution) {
    const float p = static_cast<float>(period);
    if (y == 0 && x < 4) return glm::vec3(x == 0 ? 0.0f : (x == 1 ? p
        : (x == 2 ? -p : p / static_cast<float>(resolution) * 0.5f)));
    const float u = (static_cast<float>(x)+0.5f)/static_cast<float>(width);
    const float v = (static_cast<float>(y)+0.5f)/static_cast<float>(height);
    return {(-1.5f+u*4.0f)*p,(-0.75f+v*2.0f)*p,(-0.5f+u*0.75f+v*1.25f)*p};
}
void run(const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    constexpr int width = 128, height = 64;
    unsigned int framebuffer = 0, destination = 0, vao = 0;
    glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glGenTextures(1,&destination); glBindTexture(GL_TEXTURE_2D,destination);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,destination,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Noise framebuffer incomplete");
    glGenVertexArrays(1,&vao);
    Shader shader(std::filesystem::path(MYRENDERER_SOURCE_DIR)/"shaders/fullscreen.vert",
        std::filesystem::path(MYRENDERER_SOURCE_DIR)/"shaders/cloud_noise_test.frag");
    unsigned int unpackBuffer=0;
    glGenBuffers(1,&unpackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,16,nullptr,GL_STATIC_DRAW);
    CloudNoiseTexture texture;
    OpenGlStateCache state;
    std::ofstream report(output/"metrics.json");
    report << "{\n  \"gpu\": \"" << glGetString(GL_RENDERER) << "\",\n  \"measurements\": [\n";
    bool first = true;
    for (int resolution : {32,64}) for (int period : {3,4,5}) {
        const auto volume = cloud::generateNoiseVolume(resolution,period);
        const auto name = std::to_string(resolution)+"-period-"+std::to_string(period);
        const auto asset = output/(name+".cloudnoise");
        cloud::writeNoiseVolume(asset,volume);
        const auto loaded = cloud::readNoiseVolume(asset);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_3D,0);
        glPixelStorei(GL_UNPACK_ALIGNMENT,8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,71);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,69);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
        glPixelStorei(GL_UNPACK_SKIP_IMAGES,1);
        glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE);
        texture.upload(loaded);
        int active=0,binding=0,alignment=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_TEXTURE_BINDING_3D,&binding);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
        require(active==GL_TEXTURE3 && binding==0 && alignment==8,"Upload must restore GL state");
        for (const auto setting : {GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_PIXELS,
            GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES}) {
            int value=0;glGetIntegerv(setting,&value);
            const int expected=setting==GL_UNPACK_ROW_LENGTH?71:(setting==GL_UNPACK_IMAGE_HEIGHT?69
                :(setting==GL_UNPACK_SKIP_PIXELS?2:(setting==GL_UNPACK_SKIP_ROWS?3:1)));
            require(value==expected,"Upload must restore unpack stride/offset/endian settings");
        }
        int bufferBinding=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&bufferBinding);
        require(static_cast<unsigned int>(bufferBinding)==unpackBuffer,"Upload must restore PBO binding");
        const auto id = texture.texture(); texture.upload(loaded);
        require(texture.texture()==id,"Unchanged asset must not reupload");
        auto invalid=loaded; invalid.texels.pop_back();
        bool rejected=false; try{texture.upload(invalid);}catch(const std::exception&){rejected=true;}
        require(rejected && texture.texture()==id && texture.fingerprint()==volume.fingerprint(),
            "Invalid upload must preserve the previous valid texture");
        RenderPassContext context("Cloud noise parity");
        context.inputs={"Validated offline cloud noise"}; context.outputs={"RGBA32F sampled noise"};
        context.viewportWidth=width;context.viewportHeight=height;context.state.depthTest=false;
        context.state.depthWrite=false;
        RenderPassSequence passes(width,height);
        passes.add(context,[&]{
            glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
            shader.use();shader.setInt("uCloudLightingLut",5);shader.setBool("uTestLighting",false);shader.setInt("uCloudNoiseVolume",0);shader.setFloat("uCloudNoisePeriod",static_cast<float>(period));
            shader.setInt("uWidth",width);shader.setInt("uHeight",height);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_3D,texture.texture());
            glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
        });
        passes.run([&](std::size_t,const RenderPassContext& c){state.apply(c.state);glViewport(0,0,c.viewportWidth,c.viewportHeight);});
        std::vector<float> pixels(width*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());
        require(glGetError()==GL_NO_ERROR,"Noise sampling GL error");
        float worst=0.0f;double sum=0.0,bodySquared=0.0;float bodyWorst=0.0f;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const auto p=position(x,y,width,height,period,resolution);
            const auto cpu=loaded.sample(p.x,p.y,p.z);
            const auto index=static_cast<std::size_t>((y*width+x)*4);
            for(std::size_t c=0;c<4;++c){
                require(std::isfinite(pixels[index+c]),"GPU sample must be finite");
                const float error=std::abs(cpu[c]-pixels[index+c]);worst=std::max(worst,error);sum+=error;
            }
            // This is approximation error, separate from CPU/GPU sampler parity.
            const float reference=myrenderer_cloud_worley3(p.x,p.y,p.z,period,101)*0.85f
                +myrenderer_cloud_worley3(p.x*2.0f,p.y*2.0f,p.z*2.0f,period*2,154)*0.15f;
            const float error=std::abs(reference-(cpu[0]*0.85f+cpu[1]*0.15f));
            bodyWorst=std::max(bodyWorst,error);bodySquared+=static_cast<double>(error)*error;
        }
        require(worst<2.0e-5f,"Offline noise CPU/GPU sampler parity failed");
        const double rmse=std::sqrt(bodySquared/(width*height));
        if(resolution==64 && period==4)require(rmse<0.015,"64 cubed body approximation regressed");
        if(!first) report<<",\n";
        first=false;
        report<<"    {\"resolution\": "<<resolution<<", \"period\": "<<period<<", \"fingerprint\": \""
            <<volume.fingerprint()<<"\", \"samplerMaxError\": "<<worst<<", \"samplerMae\": "<<sum/(width*height*4)
            <<", \"bodyRmse\": "<<rmse<<", \"bodyMaxError\": "<<bodyWorst<<"}";
        if (resolution==32 && period==3) {
            auto changed=loaded; changed.texels[0]^=1;
            glBindTexture(GL_TEXTURE_3D,texture.texture());
            texture.upload(changed);
            int replacementBinding=0;glGetIntegerv(GL_TEXTURE_BINDING_3D,&replacementBinding);
            require(texture.fingerprint()!=loaded.fingerprint()
                && static_cast<unsigned int>(replacementBinding)==texture.texture(),
                "Changed content must replace the asset without leaving a deleted texture bound");
        }
        std::cout<<name<<": sampler max="<<worst<<", body RMSE="<<rmse<<'\n';
    }
    shader.use();shader.setBool("uTestLighting",true);
    glActiveTexture(GL_TEXTURE3);glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,71);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
    texture.bindCanonical(shader,true,4,4);
    int active=0,buffer=0,rows=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rows);
    require(active==GL_TEXTURE3 && static_cast<unsigned int>(buffer)==unpackBuffer && rows==71,"LUT upload must restore active/PBO/unpack state");
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    std::vector<float> lighting(width*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,lighting.data());
    require(glGetError()==GL_NO_ERROR,"LUT GPU sampling GL error");
    float lutParity=0,approximation=0;
    for(int i=0;i<width*height;++i){const float depth=float(i)*40.0f/float(width*height-1);
        lutParity=std::max(lutParity,std::abs(lighting[i*4]-cloud::canonicalLightingLut().sample(depth)));
        approximation=std::max(approximation,std::abs(lighting[i*4]-std::exp(-depth)));}
    require(lutParity<2e-6f && approximation<3e-6f,"LUT CPU/GPU or analytic approximation regression");
    std::cout<<"Lighting LUT GPU max parity="<<lutParity<<", approximation="<<approximation<<'\n';
    report<<"\n  ]\n}\n";report.flush();require(static_cast<bool>(report),"Cannot write parity metrics");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glBindTexture(GL_TEXTURE_3D,0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&unpackBuffer);
    glDeleteVertexArrays(1,&vao);glDeleteTextures(1,&destination);glDeleteFramebuffers(1,&framebuffer);
}
}
int main(int argc,char**argv){
    if(argc!=2){std::cerr<<"Output directory required\n";return 1;}
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(32,32,"Cloud noise parity",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}glfwMakeContextCurrent(window);
    if(!gladLoadGL(glfwGetProcAddress)){glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;try{run(argv[1]);}catch(const std::exception&error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
