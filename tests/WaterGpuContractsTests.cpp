#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <iterator>
#include <string>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include "render/Shader.h"
#include "render/WaterWaves.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void run(const std::filesystem::path& output) {
    const auto root = std::filesystem::path(MYRENDERER_SOURCE_DIR) / "shaders";
    unsigned int vao=0, texture=0, framebuffer=0, input=0, feedback=0;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "medium test framebuffer");
    glViewport(0,0,1,1);
    Shader medium(root / "fullscreen.vert", root / "water_medium_test.frag");
    struct Interval { glm::vec4 input; bool hit; float expected; };
    // Analytic ray/plane intersections and real receiver distances, including
    // a nonzero wave height above the mean plane and the two-centimetre crossing.
    const std::array<Interval,9> intervals{{
        {{24,-2,0,1},false,2}, {{24,-2,0,0.5f},false,4},
        {{24,-2,0,-1},false,24}, {{24,-2,0,0},false,24},
        {{1,-2,0,1},false,1}, {{3,-2,0,1},true,3},
        {{24,0.48f,0.5f,1},false,0.02f},
        {{24,0.52f,0.5f,1},false,0}, {{100,-2,0,-1},false,24}
    }};
    float mediumError=0;
    for (const auto& interval : intervals) {
        medium.use(); medium.setVec4("uInterval", interval.input);
        medium.setBool("uInterfaceHit", interval.hit);
        glDrawArrays(GL_TRIANGLES,0,3);
        float pixel[4]{}; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,pixel);
        mediumError=std::max(mediumError,std::abs(pixel[0]-interval.expected));
        require(std::isfinite(pixel[0]) && std::abs(pixel[0]-interval.expected)<1.0e-5f,
            "water fog must stop at the interface, near receiver, or bounded deep segment");
    }
    // Exercise the actual visible-sky shader against an empty cube. Its disk
    // must be circular, bounded, and have a subpixel edge rather than cube texels.
    unsigned int cube=0;
    glGenTextures(1,&cube); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
    const float black[3]{};
    for(int face=0;face<6;++face) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,
        0,GL_RGB32F,1,1,0,GL_RGB,GL_FLOAT,black);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,128,128,0,GL_RGBA,GL_FLOAT,nullptr);
    glViewport(0,0,128,128);
    Shader sky(root / "fullscreen.vert",root / "skybox.frag");
    sky.use(); sky.setMat4("uInverseViewProjection",glm::inverse(glm::perspective(
        0.06981317f,1.0f,0.1f,100.0f)));
    sky.setVec3("uCameraPosition",glm::vec3(0));
    sky.setFloat("uEnvironmentIntensity",1); sky.setInt("uEnvironmentMap",0);
    sky.setVec3("uSunDirection",{0,0,-1}); sky.setVec3("uSunRadiance",glm::vec3(1));
    sky.setFloat("uSunAngularRadius",0.010471976f);
    std::array<float,128*128*4> disk{};
    sky.setBool("uAnalyticSun",false); glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(0,0,128,128,GL_RGBA,GL_FLOAT,disk.data());
    for(int pixel=0;pixel<128*128;++pixel) require(disk[static_cast<std::size_t>(pixel)*4]==0,
        "legacy sky must preserve the bound environment without a second sun");
    sky.setBool("uAnalyticSun",true); glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(0,0,128,128,GL_RGBA,GL_FLOAT,disk.data());
    int sunPixels=0, partialPixels=0;
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const float value=disk[static_cast<std::size_t>(y*128+x)*4];
        require(std::isfinite(value)&&value>=0&&value<=1,"analytic solar coverage must be bounded");
        if(value>0) ++sunPixels;
        if(value>0&&value<1) ++partialPixels;
        require(std::abs(value-disk[static_cast<std::size_t>(x*128+y)*4])<0.01f,
            "solar circle must be symmetric across the image diagonal");
    }
    require(sunPixels>1100 && sunPixels<1300 && partialPixels>30,
        "0.6-degree sun must have the correct projected area and a filtered edge");
    glDeleteTextures(1,&cube);
    Shader wave(root / "water.vert", root / "water_medium_test.frag");
    wave.use(); int program=0; glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    // Shader releases its stage objects after linking. Reattach the unmodified
    // production vertex source before relinking its transform-feedback outputs.
    std::ifstream vertexFile(root / "water.vert");
    const std::string vertexSource((std::istreambuf_iterator<char>(vertexFile)),{});
    const char* vertexText=vertexSource.c_str();
    const auto vertex=glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex,1,&vertexText,nullptr); glCompileShader(vertex);
    int compiled=0; glGetShaderiv(vertex,GL_COMPILE_STATUS,&compiled);
    require(compiled==GL_TRUE,"production vertex stage compilation");
    glAttachShader(static_cast<unsigned int>(program),vertex);
    const char* varyings[]{"vWorldPosition","vNormal"};
    glTransformFeedbackVaryings(static_cast<unsigned int>(program),2,varyings,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(static_cast<unsigned int>(program));
    glDeleteShader(vertex);
    int linked=0; glGetProgramiv(static_cast<unsigned int>(program),GL_LINK_STATUS,&linked);
    require(linked==GL_TRUE,"production water vertex shader feedback link");
    const float points[]{0,0,1,1};
    glGenBuffers(1,&input); glBindBuffer(GL_ARRAY_BUFFER,input);
    glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);
    glGenBuffers(1,&feedback); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,6*sizeof(float),nullptr,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    float positionError=0, normalError=0; int waveCases=0;
    glEnable(GL_RASTERIZER_DISCARD);
    for (auto tier : {WaterQuality::Low,WaterQuality::High}) {
        for (auto preset : {WaterPreset::Calm,WaterPreset::Windy,WaterPreset::Storm}) {
            for (float time : {0.0f,1.25f,5.0f}) {
                WaterSettings settings; water::applyPreset(settings,preset);
                settings.quality=tier; settings.surfaceOptics=true;
                settings.waveDiversity=1; settings.nearMeshFocus=1;
                settings.extent=2000; settings.timeSeconds=time;
                const glm::vec2 eyeXZ(4,-7);
                wave.use(); wave.setMat4("uCurrentViewProjection",glm::mat4(1));
                wave.setMat4("uPreviousViewProjection",glm::mat4(1));
                wave.setVec3("uCameraPosition",glm::vec3(eyeXZ.x,1,eyeXZ.y));
                wave.setFloat("uExtent",settings.extent);
                wave.setFloat("uGridResolution",static_cast<float>(tier==WaterQuality::Low
                    ? water::lowGridResolution : water::gridResolution));
                wave.setFloat("uNearMeshFocus",1); wave.setFloat("uLevel",settings.level);
                wave.setFloat("uTime",time); wave.setFloat("uPreviousTime",time);
                wave.setFloat("uSpeed",settings.speed); wave.setFloat("uSteepness",settings.steepness);
                wave.setFloat("uWaveDiversity",1);
                const auto components=water::components(settings);
                wave.setVec4Array("uWaves[0]",components.data(),components.size());
                wave.setInt("uWaveCount",water::activeComponentCount(settings));
                for (int point=0;point<2;++point) {
                    glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS,point,1); glEndTransformFeedback();
                    float data[6]{};
                    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof(data),data);
                    if(point==0) {
                        const auto cpu=water::evaluate(settings,eyeXZ);
                        const float pError=glm::length(glm::vec3(data[0],data[1],data[2])-cpu.position);
                        const float nError=glm::length(glm::vec3(data[3],data[4],data[5])-cpu.normal);
                        positionError=std::max(positionError,pError); normalError=std::max(normalError,nError);
                        require(pError<0.0002f && nError<0.0002f,"near mesh waves must match shared CPU surface");
                        require(std::abs(water::surfaceHeight(settings,{data[0],data[2]})-data[1])<0.0002f,
                            "camera medium query must match the displaced GPU wave height");
                    } else {
                        require(std::abs(data[1]-settings.level)<1.0e-5f && std::abs(data[4]-1)<1.0e-5f,
                            "unresolved horizon waves must be filtered instead of becoming false triangles");
                    }
                    ++waveCases;
                }
            }
        }
    }
    glDisable(GL_RASTERIZER_DISCARD);
    glDeleteBuffers(1,&feedback); glDeleteBuffers(1,&input);
    glDeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&texture); glDeleteVertexArrays(1,&vao);
    require(glGetError()==GL_NO_ERROR,"water contracts must not leave GL errors");
    std::ofstream report(output/"gpu-contracts.json");
    report << "{\"mediumCases\":9,\"waveCases\":" << waveCases
        << ",\"solarPixels\":" << sunPixels << ",\"solarEdgePixels\":" << partialPixels
        << ",\"maxMediumError\":" << mediumError << ",\"maxPositionError\":" << positionError
        << ",\"maxNormalError\":" << normalError << "}\n";
    require(static_cast<bool>(report),"write water contract receipt");
}
}
int main(int argc,char** argv) {
    if(argc!=2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(32,32,"Water GPU contracts",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);
    int result=0;
    try {
        require(gladLoadGL(glfwGetProcAddress)!=0,"load real GPU context");
        std::filesystem::create_directories(argv[1]); run(argv[1]);
        std::cout << "Water GPU mesh and medium contracts: PASS\n";
    } catch(const std::exception& error){std::cerr << error.what() << '\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
