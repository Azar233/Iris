#include "plugin/ShaderSceneRenderer.h"
#include "render/Shader.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "render/RenderTarget.h"
#include "render/OpenGlStateCache.h"
#include "asset/InputManifest.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace iris {
namespace {
constexpr std::size_t maximumBufferBytes = 256U * 1024U * 1024U;
struct PreparationBindings {
    GLint texture{0}, draw{0}, read{0};
    PreparationBindings() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    }
    ~PreparationBindings() {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
    }
};
struct Buffer {
    std::array<GLuint, 2> textures{}, framebuffers{};
    int width{0}, height{0};
    ~Buffer() { glDeleteFramebuffers(2, framebuffers.data()); glDeleteTextures(2, textures.data()); }
};
struct Pass {
    std::unique_ptr<Shader> shader;
    std::array<GLuint, 4> samplers{};
    std::size_t shaderRevision{0};
    ~Pass() { glDeleteSamplers(4, samplers.data()); }
};
bool sameMatrix(const glm::mat4& a, const glm::mat4& b) {
    return std::equal(glm::value_ptr(a), glm::value_ptr(a) + 16, glm::value_ptr(b));
}
std::vector<std::unique_ptr<Buffer>> allocateBuffers(const ShaderScenePackage& package, int width, int height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) throw std::runtime_error("ShaderScene extent must be 1..8192");
    GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum) throw std::runtime_error("ShaderScene extent exceeds GL limit");
    std::size_t bytes = 0;
    for (const auto& pass : package.passes) if (pass.id != "Image")
        bytes += static_cast<std::size_t>((width + pass.sizeDivisor - 1) / pass.sizeDivisor)
            * static_cast<std::size_t>((height + pass.sizeDivisor - 1) / pass.sizeDivisor) * 16U;
    if (bytes > maximumBufferBytes) throw std::runtime_error("ShaderScene buffers exceed 256 MiB budget");
    PreparationBindings bindings;
    std::vector<std::unique_ptr<Buffer>> buffers(package.passes.size());
    for (std::size_t i = 0; i < package.passes.size(); ++i) {
        const auto& pass = package.passes[i]; if (pass.id == "Image") continue;
        auto buffer = std::make_unique<Buffer>();
        buffer->width = (width + pass.sizeDivisor - 1) / pass.sizeDivisor;
        buffer->height = (height + pass.sizeDivisor - 1) / pass.sizeDivisor;
        glGenTextures(2, buffer->textures.data()); glGenFramebuffers(2, buffer->framebuffers.data());
        for (std::size_t side = 0; side < 2; ++side) {
            glBindTexture(GL_TEXTURE_2D, buffer->textures[side]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, buffer->width, buffer->height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffers[side]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffer->textures[side], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
                throw std::runtime_error("ShaderScene framebuffer allocation failed: " + pass.id);
        }
        buffers[i] = std::move(buffer);
    }
    return buffers;
}
}
struct ShaderSceneRenderer::State {
    ShaderScenePackage package;
    std::vector<std::unique_ptr<Pass>> passes;
    std::vector<std::unique_ptr<Buffer>> buffers;
    int width{0}, height{0};
    bool invalidated{true}, rendered{false}, historyValid{false};
    int readSide{0}, writeSide{1}, frameIndex{0};
    float time{0}, timeDelta{0};
    glm::mat4 view{1}, projection{1};
};
ShaderSceneRenderer::ShaderSceneRenderer(const std::filesystem::path& directory) : vertex_(directory / "fullscreen.vert") {}
ShaderSceneRenderer::~ShaderSceneRenderer() = default;
void ShaderSceneRenderer::prepareResources(const RendererSettings& settings, int width, int height) {
    auto candidate = std::make_unique<State>(); std::string error;
    const auto input = capture::snapshot(std::filesystem::u8path(settings.shaderSceneResource));
    if (!loadShaderScenePackage(std::filesystem::u8path(settings.shaderSceneResource), candidate->package, error)) throw std::runtime_error(error);
    for (const auto& description : candidate->package.passes) {
        auto pass = std::make_unique<Pass>();
        Shader::SourcePolicy policy; policy.fragmentRoot = candidate->package.root;
        policy.uniforms = {{"irisTime", GL_FLOAT}, {"irisTimeDelta", GL_FLOAT}, {"irisFrame", GL_INT},
            {"irisHistoryValid", GL_BOOL}, {"irisResolution", GL_FLOAT_VEC2}, {"irisViewportResolution", GL_FLOAT_VEC2},
            {"irisCameraPosition", GL_FLOAT_VEC3}, {"irisView", GL_FLOAT_MAT4}, {"irisProjection", GL_FLOAT_MAT4}};
        for (const auto& parameter : candidate->package.parameters.descriptors()) policy.uniforms.emplace_back(parameter.id, GL_FLOAT);
        for (int slot = 0; slot < 4; ++slot) {
            const bool declared = std::any_of(description.channels.begin(), description.channels.end(),
                [slot](const auto& channel) { return channel.slot == slot; });
            policy.uniforms.emplace_back("irisChannel" + std::to_string(slot), declared ? GL_SAMPLER_2D : 0U);
            policy.uniforms.emplace_back("irisChannelResolution" + std::to_string(slot), GL_FLOAT_VEC2);
        }
        try { pass->shader = std::make_unique<Shader>(vertex_, description.fragment, std::move(policy)); }
        catch (const std::exception& exception) { throw std::runtime_error("ShaderScene Pass " + description.id + ": " + exception.what()); }
        pass->shaderRevision = pass->shader->revision();
        for (const auto& channel : description.channels) {
            auto& sampler = pass->samplers[static_cast<std::size_t>(channel.slot)]; glGenSamplers(1, &sampler);
            glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, channel.linearFilter ? GL_LINEAR : GL_NEAREST);
            glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, channel.linearFilter ? GL_LINEAR : GL_NEAREST);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, channel.repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, channel.repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        }
        candidate->passes.push_back(std::move(pass));
    }
    candidate->buffers = allocateBuffers(candidate->package, width, height);
    candidate->width = width; candidate->height = height;
    for (const auto& pass : candidate->passes) if (!pass->shader->sourcesCurrent()) throw std::runtime_error("ShaderScene sources changed during preparation");
    if (!capture::same(input, capture::snapshot(candidate->package.manifest))) throw std::runtime_error("ShaderScene manifest changed during preparation");
    state_.swap(candidate);
}
void ShaderSceneRenderer::invalidateHistory() { if (state_) state_->invalidated = true; }
void ShaderSceneRenderer::renderFrame(const RenderPluginFrame& frame) {
    if (!state_) throw std::runtime_error("ShaderScene not prepared");
    if (!std::isfinite(frame.timeSeconds) || frame.width < 1 || frame.height < 1
        || frame.target.width() != frame.width || frame.target.height() != frame.height) throw std::runtime_error("Invalid ShaderScene frame");
    auto& state = *state_;
    if (frame.width != state.width || frame.height != state.height) {
        auto buffers = allocateBuffers(state.package, frame.width, frame.height);
        state.buffers.swap(buffers); state.width = frame.width; state.height = frame.height; state.invalidated = true;
    }
    for (auto& pass : state.passes) if (pass->shaderRevision != pass->shader->revision()) {
        state.invalidated = true; pass->shaderRevision = pass->shader->revision();
    }
    const auto view = frame.camera.viewMatrix();
    const auto projection = frame.camera.projectionMatrix(static_cast<float>(frame.width) / frame.height);
    const float delta = frame.timeSeconds - state.time;
    const bool reset = state.invalidated || !state.rendered || !sameMatrix(view, state.view) || !sameMatrix(projection, state.projection)
        || delta < 0 || delta > shaderSceneMaximumTimeStep || state.frameIndex == std::numeric_limits<int>::max();
    OpenGlStateCache cache; RenderState desired; desired.depthTest = false; desired.depthWrite = false; cache.apply(desired);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (reset) {
        const GLfloat zero[4]{};
        for (const auto& buffer : state.buffers) if (buffer) for (const auto framebuffer : buffer->framebuffers) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); glClearBufferfv(GL_COLOR, 0, zero);
        }
        state.readSide = 0; state.writeSide = 1; state.frameIndex = 0; state.timeDelta = 0; state.historyValid = false;
    } else if (delta > 0) {
        state.readSide = state.writeSide; state.writeSide = 1 - state.readSide;
        ++state.frameIndex; state.timeDelta = delta; state.historyValid = true;
    }
    // Repeated time uses the same immutable previous side and recomputes identically.
    GLint active = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    const bool labels = GLAD_GL_KHR_debug && glPushDebugGroup && glPopDebugGroup;
    for (const auto index : state.package.executionOrder) {
        const auto& description = state.package.passes[index]; auto& pass = *state.passes[index];
        const auto* buffer = state.buffers[index].get();
        const int width = buffer ? buffer->width : frame.width, height = buffer ? buffer->height : frame.height;
        const auto label = "ShaderScene " + description.id;
        if (labels) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, label.c_str());
        if (buffer) glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffers[static_cast<std::size_t>(state.writeSide)]);
        else frame.target.bindFinal();
        glViewport(0, 0, width, height);
        const GLfloat background[4]{0, 0, 0, buffer ? 0.0f : 1.0f}; glClearBufferfv(GL_COLOR, 0, background);
        pass.shader->use();
        pass.shader->setFloat("irisTime", frame.timeSeconds); pass.shader->setFloat("irisTimeDelta", state.timeDelta);
        pass.shader->setInt("irisFrame", state.frameIndex); pass.shader->setBool("irisHistoryValid", state.historyValid);
        pass.shader->setVec2("irisResolution", glm::vec2(width, height));
        pass.shader->setVec2("irisViewportResolution", glm::vec2(frame.width, frame.height));
        pass.shader->setVec3("irisCameraPosition", frame.camera.position()); pass.shader->setMat4("irisView", view);
        pass.shader->setMat4("irisProjection", projection);
        for (const auto& parameter : state.package.parameters.descriptors())
            pass.shader->setFloat(parameter.id.c_str(), state.package.parameters.value(parameter.id)->number);
        for (int slot = 0; slot < 4; ++slot) { glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_2D, 0); glBindSampler(static_cast<GLuint>(slot), 0); }
        for (const auto& channel : description.channels) {
            const auto& source = *state.buffers[channel.sourcePass];
            glActiveTexture(GL_TEXTURE0 + channel.slot);
            const auto side = static_cast<std::size_t>(channel.previousFrame ? state.readSide : state.writeSide);
            glBindTexture(GL_TEXTURE_2D, source.textures[side]); glBindSampler(static_cast<GLuint>(channel.slot), pass.samplers[static_cast<std::size_t>(channel.slot)]);
            const auto name = "irisChannel" + std::to_string(channel.slot); pass.shader->setInt(name.c_str(), channel.slot);
            pass.shader->setVec2(("irisChannelResolution" + std::to_string(channel.slot)).c_str(), glm::vec2(source.width, source.height));
        }
        glBindVertexArray(frame.fullscreenVertexArray); glDrawArrays(GL_TRIANGLES, 0, 3);
        if (labels) glPopDebugGroup();
    }
    for (int slot = 0; slot < 4; ++slot) { glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_2D, 0); glBindSampler(static_cast<GLuint>(slot), 0); }
    glActiveTexture(static_cast<GLenum>(active)); glBindVertexArray(0); glUseProgram(0); frame.target.unbind();
    state.rendered = true; state.invalidated = false; state.time = frame.timeSeconds; state.view = view; state.projection = projection;
}
RenderPluginFrameInfo ShaderSceneRenderer::frameInfo() const {
    RenderPluginFrameInfo info;
    if (state_) for (const auto index : state_->package.executionOrder) info.passNames.push_back("ShaderScene " + state_->package.passes[index].id);
    info.drawCalls = info.passNames.size(); return info;
}
std::size_t ShaderSceneRenderer::estimatedBytes() const {
    std::size_t bytes = 0;
    if (state_) for (const auto& buffer : state_->buffers) if (buffer)
        bytes += static_cast<std::size_t>(buffer->width) * static_cast<std::size_t>(buffer->height) * 16U;
    return bytes;
}
std::vector<RenderPassContext> ShaderSceneRenderer::passContexts() const {
    std::vector<RenderPassContext> contexts;
    if (!state_) return contexts;
    for (const auto index : state_->package.executionOrder) {
        const auto& description = state_->package.passes[index];
        RenderPassContext context("ShaderScene " + description.id);
        const auto* buffer = state_->buffers[index].get();
        context.viewportWidth = buffer ? buffer->width : state_->width;
        context.viewportHeight = buffer ? buffer->height : state_->height;
        context.clearMask = GL_COLOR_BUFFER_BIT; context.state.depthTest = false; context.state.depthWrite = false;
        context.inputs = {"ShaderScene + Camera + Time"};
        for (const auto& channel : description.channels) context.inputs.push_back(channel.source + (channel.previousFrame ? " previous" : " current"));
        context.outputs = {description.id == "Image" ? "DisplayColor" : description.id + " linear RGBA16F"};
        contexts.push_back(std::move(context));
    }
    return contexts;
}
}
