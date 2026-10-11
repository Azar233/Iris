#include "plugin/ShaderSceneRenderer.h"
#include "render/Shader.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include "render/OpenGlStateCache.h"
#include "asset/InputManifest.h"
#include <stdexcept>

namespace iris {
ShaderSceneRenderer::ShaderSceneRenderer(const std::filesystem::path& directory) : vertex_(directory / "fullscreen.vert") {}
ShaderSceneRenderer::~ShaderSceneRenderer() = default;
void ShaderSceneRenderer::prepareResources(const RendererSettings& settings, int width, int height) {
    if (width <= 0 || height <= 0) throw std::runtime_error("Invalid ShaderScene extent");
    ShaderScenePackage candidate; std::string error;
    const auto input = capture::snapshot(std::filesystem::u8path(settings.shaderSceneResource));
    if (!loadShaderScenePackage(std::filesystem::u8path(settings.shaderSceneResource), candidate, error))
        throw std::runtime_error(error);
    Shader::SourcePolicy policy;
    policy.fragmentRoot = candidate.root;
    policy.uniforms = {{"irisTime", GL_FLOAT}, {"irisResolution", GL_FLOAT_VEC2},
        {"irisCameraPosition", GL_FLOAT_VEC3}, {"irisView", GL_FLOAT_MAT4}, {"irisProjection", GL_FLOAT_MAT4}};
    for (const auto& parameter : candidate.parameters.descriptors())
        policy.uniforms.emplace_back(parameter.id, GL_FLOAT);
    auto shader = std::make_unique<Shader>(vertex_, candidate.fragment, std::move(policy));
    if (!capture::same(input, capture::snapshot(candidate.manifest)))
        throw std::runtime_error("ShaderScene manifest changed during preparation");
    package_ = std::move(candidate); shader_.swap(shader);
}
void ShaderSceneRenderer::renderFrame(const RenderPluginFrame& frame) {
    if (!shader_) throw std::runtime_error("ShaderScene not prepared");
    OpenGlStateCache state; RenderState desired;
    desired.depthTest = false; desired.depthWrite = false;
    state.apply(desired);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    frame.target.bindFinal(); glViewport(0, 0, frame.width, frame.height);
    const bool labels = GLAD_GL_KHR_debug && glPushDebugGroup && glPopDebugGroup;
    if (labels) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "ShaderScene Image");
    const GLfloat background[4]{0, 0, 0, 1};
    glClearBufferfv(GL_COLOR, 0, background);
    shader_->use();
    shader_->setFloat("irisTime", frame.timeSeconds);
    shader_->setVec2("irisResolution", glm::vec2(frame.width, frame.height));
    shader_->setVec3("irisCameraPosition", frame.camera.position());
    shader_->setMat4("irisView", frame.camera.viewMatrix());
    shader_->setMat4("irisProjection", frame.camera.projectionMatrix(static_cast<float>(frame.width) / frame.height));
    for (const auto& parameter : package_.parameters.descriptors())
        shader_->setFloat(parameter.id.c_str(), package_.parameters.value(parameter.id)->number);
    glBindVertexArray(frame.fullscreenVertexArray); glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0); glUseProgram(0); frame.target.unbind();
    if (labels) glPopDebugGroup();
}
RenderPluginFrameInfo ShaderSceneRenderer::frameInfo() const {
    RenderPluginFrameInfo info; info.passNames = {"ShaderScene Image"}; info.drawCalls = 1; return info;
}
}
