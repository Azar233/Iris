#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "render/Camera.h"
#include "render/CloudShadowRenderer.h"
#include "render/GodRaysRenderer.h"
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
unsigned int makeTexture(int channels, const std::vector<float>& pixels, int width, int height) {
    unsigned int texture = 0;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, channels == 4 ? GL_RGBA32F : GL_R32F,
        width, height, 0, channels == 4 ? GL_RGBA : GL_RED, GL_FLOAT, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}
std::vector<float> read(unsigned int texture, int width, int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width) * height);
    glBindTexture(GL_TEXTURE_2D, texture); glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, pixels.data());
    return pixels;
}
void run(const std::filesystem::path& output) {
    constexpr int size = 64;
    const auto directory = std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders";
    Camera camera;
    camera.setOrbitPose(glm::vec3(0), 180, -20, 10, 60);
    atmosphere::AtmosphereParameters p;
    p.enabled = p.cloudsEnabled = p.cloudShadowsEnabled = p.cloudGodRaysEnabled = true;
    p.sunElevationDegrees = 20; p.sunAzimuthDegrees = 0;
    p.cloudCoverage = p.cloudCoverageVariation = 0;
    CloudShadowRenderer shadow(directory);
    shadow.render(p, camera.position(), 150, 0.0025f);
    GodRaysRenderer rays(directory);
    std::vector<float> clouds(size * size * 4, 0.0f), depth(size * size, 1.0f);
    for (int i = 0; i < size * size; ++i) clouds[i*4+3] = 1;
    unsigned int cloudTexture = makeTexture(4, clouds, size, size);
    unsigned int depthTexture = makeTexture(1, depth, size, size);
    const auto render = [&] { return rays.render(camera, p, shadow, cloudTexture, depthTexture, size, size); };
    require(render(), "a visible daylight sun must run god rays");
    const auto clear = read(rays.texture(), size/2, size/2);
    float error = 0;
    for (int y=0; y<size/2; ++y) for (int x=0; x<size/2; ++x) {
        const float u=(x+0.5f)/(size/2)-0.5f, v=(y+0.5f)/(size/2)-0.5f;
        const float expected = p.cloudGodRaysStrength * std::exp(-4*(u*u+v*v));
        const float actual = clear[static_cast<std::size_t>(y)*(size/2)+x];
        require(std::isfinite(actual) && actual >= 0 && actual <= p.cloudGodRaysStrength,
            "scattering must be bounded and finite");
        error = std::max(error, std::abs(actual-expected));
    }
    require(error < 0.0001f, "unoccluded radial integration must match its analytic reference");
    require(render() && clear == read(rays.texture(), size/2,size/2), "fixed inputs must repeat exactly");
    // A narrow occluder on the line from a sky pixel to the sun. The radial blur samples around
    // it, so a blocked line can retain radiance: record this screen-space artifact explicitly.
    for (int y=0; y<size; ++y) for (int x=40; x<44; ++x) depth[y*size+x]=0.5f;
    glBindTexture(GL_TEXTURE_2D,depthTexture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RED,GL_FLOAT,depth.data());
    require(render(), "partial occluders must run");
    const auto edge=read(rays.texture(),size/2,size/2);
    for (int y=0; y<size/2; ++y) for(int x=20; x<22; ++x)
        require(edge[static_cast<std::size_t>(y)*(size/2)+x]==0,"foreground mask must suppress direct overlay");
    double edgeReduction=0;
    for(std::size_t i=0;i<clear.size();++i) edgeReduction+=clear[i]-edge[i];
    require(edgeReduction>0,"partial geometry must attenuate the radial source mask");
    const float blockedLine=edge[16U*32U+28U];
    std::ofstream evidence(output / "occluder-edge.ppm",std::ios::binary);
    evidence << "P6\n64 32\n255\n";
    for(int y=31;y>=0;--y) for(int panel=0;panel<2;++panel) for(int x=0;x<32;++x) {
        const float value=(panel==0?clear:edge)[static_cast<std::size_t>(y)*32+x];
        const auto byte=static_cast<unsigned char>(std::clamp(value/p.cloudGodRaysStrength,0.0f,1.0f)*255+0.5f);
        const unsigned char pixel[]={byte,byte,byte}; evidence.write(reinterpret_cast<const char*>(pixel),3);
    }
    require(static_cast<bool>(evidence),"edge artifact evidence must be saved");
    std::cout << "Known screen-space occluder-edge artifact: blocked radial line scattering " << blockedLine << '\n';
    std::fill(depth.begin(),depth.end(),1.0f);
    glBindTexture(GL_TEXTURE_2D,depthTexture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RED,GL_FLOAT,depth.data());
    for (int i=0; i<size*size; ++i) clouds[i*4+3]=0;
    glBindTexture(GL_TEXTURE_2D, cloudTexture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RGBA,GL_FLOAT,clouds.data());
    require(render(), "opaque clouds still run an empty scattering pass");
    for (float value : read(rays.texture(),size/2,size/2)) require(value==0, "opaque clouds must block shafts");
    for (int i=0; i<size*size; ++i) clouds[i*4+3]=1;
    glBindTexture(GL_TEXTURE_2D,cloudTexture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RGBA,GL_FLOAT,clouds.data());
    std::fill(depth.begin(),depth.end(),0.5f);
    glBindTexture(GL_TEXTURE_2D,depthTexture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RED,GL_FLOAT,depth.data());
    require(render(), "geometry mask must run without stale radiance");
    for (float value : read(rays.texture(),size/2,size/2)) require(value==0, "foreground geometry must block shafts");
    std::fill(depth.begin(),depth.end(),1.0f);
    glBindTexture(GL_TEXTURE_2D,depthTexture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RED,GL_FLOAT,depth.data());
    p.cloudCoverage=1; p.cloudDensity=4;
    shadow.render(p,camera.position(),150,0.0025f);
    require(render(), "cloud shadow map must be consumed");
    const auto shadowed=read(rays.texture(),size/2,size/2);
    double reduction=0; for(std::size_t i=0;i<clear.size();++i) reduction += clear[i]-shadowed[i];
    require(reduction/clear.size() > 0.001, "solar transmission map must attenuate radial scattering");
    p.cloudGodRaysStrength=0; require(!render(), "zero strength must skip rays");
    p.cloudGodRaysStrength=0.08f; p.cloudGodRaysEnabled=false; require(!render(), "disabled rays must skip");
    p.cloudGodRaysEnabled=true; p.sunElevationDegrees=-10; require(!render(), "night must skip solar rays");
    p.sunElevationDegrees=20; p.sunAzimuthDegrees=180; require(!render(), "sun behind the camera must skip rays");
    p.sunAzimuthDegrees=0; p.cloudCoverage=0; p.cloudCoverageVariation=0;
    shadow.render(p,camera.position(),150,0.0025f);
    require(rays.render(camera,p,shadow,cloudTexture,depthTexture,66,34), "resize must render");
    require(rays.estimatedBytes()==33U*17U*2U, "resize must use half-resolution dimensions");
    require(glGetError()==GL_NO_ERROR,"god rays must not leak GL errors");
    glDeleteTextures(1,&cloudTexture); glDeleteTextures(1,&depthTexture);
    std::cout << "God rays analytic maximum error " << error << ", solar-map reduction "
        << reduction/clear.size() << "\nGod rays acceptance: PASS\n";
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(32,32,"God rays acceptance",nullptr,nullptr);
    if (!window) {glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);
    if(!gladLoadGL(glfwGetProcAddress)) {glfwDestroyWindow(window);glfwTerminate();return 1;}
    int result=0;
    try {std::filesystem::create_directories(argv[1]);run(argv[1]);}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
