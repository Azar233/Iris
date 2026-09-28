#include "render/CloudNoiseTexture.h"
#include "optics/CloudLightingLut.h"
#include "render/Shader.h"
#include <glad/gl.h>
#include <stdexcept>

CloudNoiseTexture::~CloudNoiseTexture() {
    glDeleteTextures(1, &texture_);
    glDeleteTextures(1, &lightingTexture_);
}
void CloudNoiseTexture::upload(const cloud::NoiseVolume& volume) {
    const auto fingerprint = volume.fingerprint(); // Validate before touching GL.
    if (texture_ != 0 && fingerprint == fingerprint_)
        return;
    int oldBinding = 0, oldAlignment = 0, oldBuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &oldBinding);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlignment);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &oldBuffer);
    const GLenum unpackSettings[] = {GL_UNPACK_ROW_LENGTH,  GL_UNPACK_IMAGE_HEIGHT,
                                     GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,
                                     GL_UNPACK_SKIP_IMAGES, GL_UNPACK_SWAP_BYTES};
    int previousUnpack[6]{};
    for (int i = 0; i < 6; ++i)
        glGetIntegerv(unpackSettings[i], &previousUnpack[i]);
    // Reject pre-existing errors rather than swallowing someone else's failure.
    if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GL error before cloud noise upload");
    unsigned int candidate = 0;
    glGenTextures(1, &candidate);
    glBindTexture(GL_TEXTURE_3D, candidate);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto setting : unpackSettings)
        glPixelStorei(setting, 0);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16, volume.resolution, volume.resolution,
                 volume.resolution, 0, GL_RGBA, GL_UNSIGNED_SHORT, volume.texels.data());
    const auto error = glGetError();
    glBindTexture(GL_TEXTURE_3D, static_cast<unsigned int>(oldBinding));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<unsigned int>(oldBuffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlignment);
    for (int i = 0; i < 6; ++i)
        glPixelStorei(unpackSettings[i], previousUnpack[i]);
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &candidate);
        throw std::runtime_error("Cloud noise 3D upload failed");
    }
    if (texture_ != 0 && static_cast<unsigned int>(oldBinding) == texture_)
        glBindTexture(GL_TEXTURE_3D, candidate);
    glDeleteTextures(1, &texture_);
    texture_ = candidate;
    bytes_ = volume.texels.size() * sizeof(std::uint16_t);
    fingerprint_ = fingerprint;
}

void CloudNoiseTexture::bindCanonical(Shader& shader, bool enabled, int period, unsigned int unit) {
    if (enabled && period != 4)
        throw std::runtime_error("Offline cloud noise requires period 4");
    if (enabled && texture_ == 0)
        upload(cloud::canonicalNoiseVolume());
    if (enabled)
        ensureLightingTexture();
    shader.setInt("uCloudLightingLut", 5);
    shader.setInt("uCloudNoiseVolume", static_cast<int>(unit));
    shader.setFloat("uCloudNoisePeriod", 4.0f);
    int active = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_3D, enabled ? texture_ : 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, enabled ? lightingTexture_ : 0);
    glActiveTexture(static_cast<GLenum>(active));
}

void CloudNoiseTexture::ensureLightingTexture() {
    if (lightingTexture_)
        return;
    const auto& lut = cloud::canonicalLightingLut();
    std::vector<float> values;
    values.reserve(lut.values.size());
    for (auto v : lut.values)
        values.push_back(static_cast<float>(v) / 16777216.0f);
    int binding = 0, buffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
    const GLenum settings[] = {GL_UNPACK_ALIGNMENT,   GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT,
                               GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,  GL_UNPACK_SKIP_IMAGES,
                               GL_UNPACK_SWAP_BYTES};
    int previous[7]{};
    for (int i = 0; i < 7; ++i)
        glGetIntegerv(settings[i], &previous[i]);
    if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GL error before cloud lighting upload");
    unsigned int candidate = 0;
    glGenTextures(1, &candidate);
    glBindTexture(GL_TEXTURE_2D, candidate);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (int i = 0; i < 7; ++i)
        glPixelStorei(settings[i], i == 0 ? 1 : 0);
    // A 129x64 atlas avoids exceeding GL 3.3's minimum maximum texture width.
    values.resize(129 * 64, 0.0f);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 129, 64, 0, GL_RED, GL_FLOAT, values.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const auto error = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(binding));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<unsigned int>(buffer));
    for (int i = 0; i < 7; ++i)
        glPixelStorei(settings[i], previous[i]);
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &candidate);
        throw std::runtime_error("Cloud lighting LUT upload failed");
    }
    lightingTexture_ = candidate;
}
