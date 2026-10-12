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
    GLint texture{0}, volume{0}, draw{0}, read{0}, unpackBuffer{0};
    std::array<GLint, 6> unpack{};
    static constexpr std::array<GLenum, 6> unpackNames{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES};
    PreparationBindings() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_TEXTURE_BINDING_3D, &volume); glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        for (std::size_t i = 0; i < unpack.size(); ++i) glGetIntegerv(unpackNames[i], &unpack[i]);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    }
    ~PreparationBindings() {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(volume)); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer));
        for (std::size_t i = 0; i < unpack.size(); ++i) glPixelStorei(unpackNames[i], unpack[i]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
    }
};
struct Buffer {
    std::array<GLuint, 2> textures{}, framebuffers{};
    int width{0}, height{0};
    int sides{2};
    ~Buffer() { glDeleteFramebuffers(2, framebuffers.data()); glDeleteTextures(2, textures.data()); }
};
struct Pass {
    std::unique_ptr<Shader> shader;
    std::array<GLuint, 4> samplers{};
    ~Pass() { glDeleteSamplers(4, samplers.data()); }
};
struct Texture {
    GLuint name{0};
    ~Texture() { glDeleteTextures(1, &name); }
};
struct FrameQuery {
    GLuint end{0};
    bool& pending;
    FrameQuery(GLuint first, GLuint last, bool& value) : end(last), pending(value) {
        if (first) glQueryCounter(first, GL_TIMESTAMP);
    }
    ~FrameQuery() { if (end) { glQueryCounter(end, GL_TIMESTAMP); pending = true; } }
};
std::vector<std::unique_ptr<Texture>> allocateTextures(const ShaderScenePackage& package) {
    PreparationBindings bindings;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (const auto field : PreparationBindings::unpackNames) glPixelStorei(field, field == GL_UNPACK_ALIGNMENT ? 1 : 0);
    std::vector<std::unique_ptr<Texture>> textures;
    for (const auto& description : package.textures) {
        GLint maximum = 0; glGetIntegerv(description.volume ? GL_MAX_3D_TEXTURE_SIZE : GL_MAX_TEXTURE_SIZE, &maximum);
        if (description.width > maximum || description.height > maximum || description.depth > maximum)
            throw std::runtime_error("Texture extent exceeds GL limit: " + description.id);
        auto texture = std::make_unique<Texture>(); glGenTextures(1, &texture->name);
        const GLenum target = description.volume ? GL_TEXTURE_3D : GL_TEXTURE_2D;
        glBindTexture(target, texture->name);
        const auto internal = description.format == ShaderSceneTextureFormat::LinearR8 ? GL_R8
            : description.format == ShaderSceneTextureFormat::LinearRgba8 ? GL_RGBA8 : GL_SRGB8_ALPHA8;
        const GLenum format = description.format == ShaderSceneTextureFormat::LinearR8 ? GL_RED : GL_RGBA;
        if (description.volume) glTexImage3D(target, 0, internal, description.width, description.height, description.depth,
            0, format, GL_UNSIGNED_BYTE, description.texels.data());
        else glTexImage2D(target, 0, internal, description.width, description.height, 0, format, GL_UNSIGNED_BYTE, description.texels.data());
        glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
        int levels = 0;
        if (description.mipmaps) for (int size = std::max({description.width, description.height, description.depth}); size > 1; size /= 2) ++levels;
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, levels);
        if (description.mipmaps) glGenerateMipmap(target);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Texture upload failed: " + description.id);
        textures.push_back(std::move(texture));
    }
    return textures;
}
bool sameMatrix(const glm::mat4& a, const glm::mat4& b) {
    return std::equal(glm::value_ptr(a), glm::value_ptr(a) + 16, glm::value_ptr(b));
}
std::vector<std::unique_ptr<Buffer>> allocateBuffers(const ShaderScenePackage& package, int width, int height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) throw std::runtime_error("ShaderScene extent must be 1..8192");
    GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum) throw std::runtime_error("ShaderScene extent exceeds GL limit");
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < package.passes.size(); ++index) {
        const auto& pass = package.passes[index]; if (pass.id == "Image") continue;
        bytes += static_cast<std::size_t>((width + pass.sizeDivisor - 1) / pass.sizeDivisor)
            * static_cast<std::size_t>((height + pass.sizeDivisor - 1) / pass.sizeDivisor) * (shaderScenePassNeedsHistory(package, index) ? 16U : 8U);
    }
    if (bytes > maximumBufferBytes) throw std::runtime_error("ShaderScene buffers exceed 256 MiB budget");
    PreparationBindings bindings;
    std::vector<std::unique_ptr<Buffer>> buffers(package.passes.size());
    for (std::size_t i = 0; i < package.passes.size(); ++i) {
        const auto& pass = package.passes[i]; if (pass.id == "Image") continue;
        auto buffer = std::make_unique<Buffer>();
        buffer->width = (width + pass.sizeDivisor - 1) / pass.sizeDivisor;
        buffer->height = (height + pass.sizeDivisor - 1) / pass.sizeDivisor;
        buffer->sides = shaderScenePassNeedsHistory(package, i) ? 2 : 1;
        glGenTextures(buffer->sides, buffer->textures.data()); glGenFramebuffers(buffer->sides, buffer->framebuffers.data());
        for (std::size_t side = 0; side < static_cast<std::size_t>(buffer->sides); ++side) {
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
    capture::InputFile manifestInput;
    std::vector<std::unique_ptr<Pass>> passes;
    std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<std::unique_ptr<Texture>> textures;
    int width{0}, height{0};
    bool invalidated{true}, rendered{false}, historyValid{false};
    int readSide{0}, writeSide{1}, frameIndex{0};
    float time{0}, timeDelta{0};
    glm::mat4 view{1}, projection{1};
    std::vector<ModuleParameterValue> parameterValues;
    std::array<GLuint, 8> timingQueries{};
    std::array<bool, 4> timingPending{};
    std::size_t timingCursor{0};
    bool gpuTimeValid{false}, gpuTimeUpdated{false};
    double gpuMilliseconds{0};
    ~State() { glDeleteQueries(static_cast<GLsizei>(timingQueries.size()), timingQueries.data()); }
};
ShaderSceneRenderer::ShaderSceneRenderer(const std::filesystem::path& directory) : vertex_(directory / "fullscreen.vert") {}
ShaderSceneRenderer::~ShaderSceneRenderer() = default;
const ShaderScenePackage* ShaderSceneRenderer::package() const { return state_ ? &state_->package : nullptr; }
ShaderSceneRuntimeStatus ShaderSceneRenderer::runtimeStatus() const {
    if (!state_) return {};
    return {state_->width, state_->height, state_->frameIndex, state_->time, state_->timeDelta,
        state_->rendered, state_->historyValid, state_->invalidated};
}
void ShaderSceneRenderer::prepareResources(const RendererSettings& settings, int width, int height) {
    capture::InputWatch inputs;
    capture::watchInput(std::filesystem::u8path(shaderSceneResourceFor(settings)));
    try {
    auto candidate = std::make_unique<State>(); std::string error;
    const auto resource = shaderSceneResourceFor(settings);
    const auto input = capture::snapshot(std::filesystem::u8path(resource));
    candidate->manifestInput = input;
    if (!loadShaderScenePackage(std::filesystem::u8path(resource), candidate->package, error)) throw std::runtime_error(error);
    for (const auto& description : candidate->package.passes) {
        auto pass = std::make_unique<Pass>();
        Shader::SourcePolicy policy; policy.fragmentRoot = candidate->package.root;
        policy.mainImageEntry = candidate->package.mainImageEntry;
        policy.passLabel=description.id;
        policy.packageOwnedReload = true;
        policy.uniforms = {{"irisTime", GL_FLOAT}, {"irisTimeDelta", GL_FLOAT}, {"irisFrame", GL_INT},
            {"irisHistoryValid", GL_BOOL}, {"irisResolution", GL_FLOAT_VEC2}, {"irisViewportResolution", GL_FLOAT_VEC2},
            {"irisCameraPosition", GL_FLOAT_VEC3}, {"irisView", GL_FLOAT_MAT4}, {"irisProjection", GL_FLOAT_MAT4},
            {"irisCameraForward", GL_FLOAT_VEC3}, {"irisCameraRight", GL_FLOAT_VEC3}, {"irisCameraUp", GL_FLOAT_VEC3},
            {"irisCameraTanHalfFov", GL_FLOAT}, {"irisCameraNear", GL_FLOAT}};
        for (const auto& parameter : candidate->package.parameters.descriptors())
            policy.uniforms.emplace_back(parameter.id, parameter.type == ModuleParameterType::Bool ? GL_BOOL : GL_FLOAT);
        for (int slot = 0; slot < 4; ++slot) {
            const auto channel = std::find_if(description.channels.begin(), description.channels.end(),
                [slot](const auto& channel) { return channel.slot == slot; });
            const auto type = channel == description.channels.end() ? 0U
                : channel->externalTexture && candidate->package.textures[channel->sourceTexture].volume ? GL_SAMPLER_3D : GL_SAMPLER_2D;
            policy.uniforms.emplace_back("irisChannel" + std::to_string(slot), type);
            policy.uniforms.emplace_back("irisChannelResolution" + std::to_string(slot), GL_FLOAT_VEC2);
            if (candidate->package.version == 3) policy.uniforms.emplace_back("irisChannelExtent" + std::to_string(slot), GL_FLOAT_VEC3);
        }
        try { pass->shader = std::make_unique<Shader>(vertex_, description.fragment, std::move(policy)); }
        catch (const ShaderSourceError&) { throw; }
        catch (const std::exception& exception) { throw std::runtime_error("ShaderScene Pass " + description.id + ": " + exception.what()); }
        for (const auto& channel : description.channels) {
            auto& sampler = pass->samplers[static_cast<std::size_t>(channel.slot)]; glGenSamplers(1, &sampler);
            glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, channel.mipmaps
                ? (channel.linearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR) : (channel.linearFilter ? GL_LINEAR : GL_NEAREST));
            glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, channel.linearFilter ? GL_LINEAR : GL_NEAREST);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, channel.repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, channel.repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, channel.repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        }
        candidate->passes.push_back(std::move(pass));
    }
    candidate->buffers = allocateBuffers(candidate->package, width, height);
    candidate->textures = allocateTextures(candidate->package);
    candidate->width = width; candidate->height = height;
    for (const auto& pass : candidate->passes) if (!pass->shader->sourcesCurrent()) throw std::runtime_error("ShaderScene sources changed during preparation");
    if (!capture::same(input, capture::snapshot(candidate->package.manifest))) throw std::runtime_error("ShaderScene manifest changed during preparation");
    if (!shaderSceneTextureInputsCurrent(candidate->package)) throw std::runtime_error("ShaderScene textures changed during preparation");
    for (auto& texture : candidate->package.textures) std::vector<unsigned char>().swap(texture.texels);
    candidate->parameterValues = shaderSceneParameterValues(candidate->package, settings);
    glGenQueries(static_cast<GLsizei>(candidate->timingQueries.size()), candidate->timingQueries.data());
    for (const auto& input : inputs.stamps) if (input.changed()) throw std::runtime_error("ShaderScene dependency changed during preparation: " + input.path.u8string());
    state_.swap(candidate);
    watchedInputs_.swap(inputs.stamps);
    reloadPending_ = false;
    } catch (...) {
        // Keep both committed and attempted dependencies so missing/new inputs can recover.
        for (const auto& input : watchedInputs_) inputs.record(input.path);
        watchedInputs_.swap(inputs.stamps); reloadPending_ = true;
        throw;
    }
}
Shader::ReloadReport ShaderSceneRenderer::reloadResources(const RendererSettings& settings, bool retryPending) {
    Shader::ReloadReport report;
    if (!state_) return report;
    report.pending = reloadPending_ ? 1 : 0;
    const bool changed = std::any_of(watchedInputs_.begin(), watchedInputs_.end(), [](const auto& input) { return input.changed(); });
    if (!changed && !(retryPending && reloadPending_)) return report;
    try {
        prepareResources(settings, state_->width, state_->height);
        report.reloaded = state_->passes.size(); report.pending = 0;
        report.message = "ShaderScene package committed; history reset";
    } catch (const ShaderSourceError& error) {
        report.failed = 1; report.diagnostics = error.diagnostics; report.message = error.what();
    } catch (const std::exception& error) {
        report.failed = 1; report.message = error.what();
    }
    if (report.failed) {
        report.pending = 1; report.retained = state_->passes.size();
        report.message = "ShaderScene package rejected; prior instance/configuration/history retained:\n" + report.message;
    }
    return report;
}
void ShaderSceneRenderer::invalidateHistory() { if (state_) state_->invalidated = true; }
void ShaderSceneRenderer::renderFrame(const RenderPluginFrame& frame) {
    if (!state_) throw std::runtime_error("ShaderScene not prepared");
    if (!std::isfinite(frame.timeSeconds) || frame.width < 1 || frame.height < 1
        || frame.target.width() != frame.width || frame.target.height() != frame.height) throw std::runtime_error("Invalid ShaderScene frame");
    auto& state = *state_;
    const auto parameters = shaderSceneParameterValues(state.package, frame.settings);
    bool parameterChanged = parameters.size() != state.parameterValues.size();
    if (!parameterChanged) for (std::size_t i = 0; i < parameters.size(); ++i)
        parameterChanged = parameterChanged || parameters[i].type != state.parameterValues[i].type
            || (parameters[i].type == ModuleParameterType::Bool ? parameters[i].boolean != state.parameterValues[i].boolean
                : parameters[i].number != state.parameterValues[i].number);
    if (capture::activeManifest) {
        if (!capture::same(state.manifestInput, capture::snapshot(state.package.manifest)))
            throw std::runtime_error("ShaderScene manifest input changed after preparation");
        capture::recordInput(state.package.manifest);
        if (!shaderSceneTextureInputsCurrent(state.package)) throw std::runtime_error("ShaderScene texture input changed after preparation");
        for (const auto& texture : state.package.textures) if (!texture.path.empty()) capture::recordInput(texture.path);
    }
    if (frame.width != state.width || frame.height != state.height) {
        auto buffers = allocateBuffers(state.package, frame.width, frame.height);
        state.buffers.swap(buffers); state.width = frame.width; state.height = frame.height; state.invalidated = true;
    }
    const auto view = frame.camera.viewMatrix();
    const auto projection = frame.camera.projectionMatrix(static_cast<float>(frame.width) / frame.height);
    const float delta = frame.timeSeconds - state.time;
    const bool reset = state.invalidated || parameterChanged || !state.rendered || !sameMatrix(view, state.view) || !sameMatrix(projection, state.projection)
        || delta < 0 || delta > shaderSceneMaximumTimeStep || state.frameIndex == std::numeric_limits<int>::max();
    state.gpuTimeUpdated = false;
    std::size_t timingIndex = state.timingPending.size();
    for (std::size_t offset = 0; offset < state.timingPending.size(); ++offset) {
        const auto index = (state.timingCursor + offset) % state.timingPending.size();
        if (state.timingPending[index]) {
            GLint available = GL_FALSE;
            glGetQueryObjectiv(state.timingQueries[index * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE) {
                GLuint64 first = 0, last = 0;
                glGetQueryObjectui64v(state.timingQueries[index * 2], GL_QUERY_RESULT, &first);
                glGetQueryObjectui64v(state.timingQueries[index * 2 + 1], GL_QUERY_RESULT, &last);
                state.gpuMilliseconds = static_cast<double>(last - first) / 1e6;
                state.gpuTimeValid = state.gpuTimeUpdated = true; state.timingPending[index] = false;
            }
        }
        if (!state.timingPending[index] && timingIndex == state.timingPending.size()) timingIndex = index;
    }
    bool unusedPending = false;
    const bool timed = timingIndex < state.timingPending.size();
    FrameQuery query(timed ? state.timingQueries[timingIndex * 2] : 0,
        timed ? state.timingQueries[timingIndex * 2 + 1] : 0, timed ? state.timingPending[timingIndex] : unusedPending);
    if (timed) state.timingCursor = (timingIndex + 1) % state.timingPending.size();
    OpenGlStateCache cache; RenderState desired; desired.depthTest = false; desired.depthWrite = false; cache.apply(desired);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (reset) {
        const GLfloat zero[4]{};
        for (const auto& buffer : state.buffers) if (buffer) for (int side = 0; side < buffer->sides; ++side) {
            glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffers[static_cast<std::size_t>(side)]); glClearBufferfv(GL_COLOR, 0, zero);
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
        if (buffer) glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffers[static_cast<std::size_t>(buffer->sides == 2 ? state.writeSide : 0)]);
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
        pass.shader->setVec3("irisCameraForward", frame.camera.forwardDirection()); pass.shader->setVec3("irisCameraRight", frame.camera.rightDirection());
        pass.shader->setVec3("irisCameraUp", frame.camera.upDirection()); pass.shader->setFloat("irisCameraNear", frame.camera.nearPlane());
        pass.shader->setFloat("irisCameraTanHalfFov", std::tan(frame.camera.fieldOfView() * 0.008726646259971648f));
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const auto& parameter = state.package.parameters.descriptors()[i];
            if (parameters[i].type == ModuleParameterType::Bool) pass.shader->setBool(parameter.id.c_str(), parameters[i].boolean);
            else pass.shader->setFloat(parameter.id.c_str(), parameters[i].number);
        }
        for (int slot = 0; slot < 4; ++slot) { glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_3D, 0); glBindSampler(static_cast<GLuint>(slot), 0); }
        for (const auto& channel : description.channels) {
            glActiveTexture(GL_TEXTURE0 + channel.slot);
            glm::vec3 extent(0);
            if (channel.externalTexture) {
                const auto& source = state.package.textures[channel.sourceTexture];
                glBindTexture(source.volume ? GL_TEXTURE_3D : GL_TEXTURE_2D, state.textures[channel.sourceTexture]->name);
                extent = glm::vec3(source.width, source.height, source.depth);
            } else {
                const auto& source = *state.buffers[channel.sourcePass];
                const auto side = static_cast<std::size_t>(source.sides == 2 ? (channel.previousFrame ? state.readSide : state.writeSide) : 0);
                glBindTexture(GL_TEXTURE_2D, source.textures[side]); extent = glm::vec3(source.width, source.height, 1);
            }
            glBindSampler(static_cast<GLuint>(channel.slot), pass.samplers[static_cast<std::size_t>(channel.slot)]);
            const auto name = "irisChannel" + std::to_string(channel.slot); pass.shader->setInt(name.c_str(), channel.slot);
            pass.shader->setVec2(("irisChannelResolution" + std::to_string(channel.slot)).c_str(), glm::vec2(extent));
            if (state.package.version == 3) pass.shader->setVec3(("irisChannelExtent" + std::to_string(channel.slot)).c_str(), extent);
        }
        glBindVertexArray(frame.fullscreenVertexArray); glDrawArrays(GL_TRIANGLES, 0, 3);
        if (labels) glPopDebugGroup();
    }
    for (int slot = 0; slot < 4; ++slot) { glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_3D, 0); glBindSampler(static_cast<GLuint>(slot), 0); }
    glActiveTexture(static_cast<GLenum>(active)); glBindVertexArray(0); glUseProgram(0); frame.target.unbind();
    state.rendered = true; state.invalidated = false; state.time = frame.timeSeconds; state.view = view; state.projection = projection;
    state.parameterValues = parameters;
}
RenderPluginFrameInfo ShaderSceneRenderer::frameInfo() const {
    RenderPluginFrameInfo info;
    if (state_) for (const auto index : state_->package.executionOrder) info.passNames.push_back("ShaderScene " + state_->package.passes[index].id);
    if (state_) { info.gpuTimeValid = state_->gpuTimeValid; info.gpuTimeUpdated = state_->gpuTimeUpdated; info.gpuMilliseconds = state_->gpuMilliseconds; }
    info.drawCalls = info.passNames.size(); return info;
}
std::size_t ShaderSceneRenderer::estimatedBytes() const {
    std::size_t bytes = 0;
    if (state_) for (const auto& buffer : state_->buffers) if (buffer)
        bytes += static_cast<std::size_t>(buffer->width) * static_cast<std::size_t>(buffer->height) * static_cast<std::size_t>(buffer->sides) * 8U;
    if (state_) for (const auto& texture : state_->package.textures) bytes += shaderSceneTextureBytes(texture);
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
