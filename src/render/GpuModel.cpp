#include "render/GpuModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "render/Mesh.h"
#include "render/Shader.h"
#include "render/Texture2D.h"

namespace {

void bindMaterialSamplers(const Shader& shader) {
    // These units are invariant for every material in one draw entry point.
    shader.setInt("uBaseColorTexture", 0);
    shader.setInt("uNormalTexture", 1);
    shader.setInt("uMetallicRoughnessTexture", 2);
    shader.setInt("uThicknessTexture", 7);
}

glm::vec3 srgbToLinear(const glm::vec3& color) {
    glm::vec3 linear(0.0f);
    for (int channel = 0; channel < 3; ++channel) {
        const float value = std::clamp(color[channel], 0.0f, 1.0f);
        linear[channel] = value <= 0.04045f
            ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
    return linear;
}

glm::vec3 sampleVector(
    const std::vector<AnimationVectorKey>& keys,
    float time,
    const glm::vec3& fallback
) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1U || time <= keys.front().timeSeconds) return keys.front().value;
    if (time >= keys.back().timeSeconds) return keys.back().value;
    const auto upper = std::upper_bound(
        keys.begin(), keys.end(), time,
        [](float value, const AnimationVectorKey& key) { return value < key.timeSeconds; }
    );
    const auto lower = std::prev(upper);
    const float span = std::max(upper->timeSeconds - lower->timeSeconds, 1.0e-6f);
    return glm::mix(lower->value, upper->value, (time - lower->timeSeconds) / span);
}

glm::quat sampleRotation(
    const std::vector<AnimationRotationKey>& keys,
    float time,
    const glm::quat& fallback
) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1U || time <= keys.front().timeSeconds) return keys.front().value;
    if (time >= keys.back().timeSeconds) return keys.back().value;
    const auto upper = std::upper_bound(
        keys.begin(), keys.end(), time,
        [](float value, const AnimationRotationKey& key) { return value < key.timeSeconds; }
    );
    const auto lower = std::prev(upper);
    const float span = std::max(upper->timeSeconds - lower->timeSeconds, 1.0e-6f);
    return glm::normalize(glm::slerp(
        lower->value,
        upper->value,
        (time - lower->timeSeconds) / span
    ));
}

} // namespace

GpuModel::GpuModel(
    ModelData data,
    TextureCache& textureCache,
    std::vector<TextureUploadWarning>& warnings
)
    : sourceData_(std::make_shared<const ModelData>(std::move(data))),
      whiteTexture_(textureCache.whiteTexture()),
      flatNormalTexture_(textureCache.flatNormalTexture()),
      linearWhiteTexture_(textureCache.linearWhiteTexture()) {
    const ModelData& modelData = *sourceData_;
    boundsCenter_ = 0.5f * (modelData.boundsMin + modelData.boundsMax);
    boundsRadius_ = glm::length(0.5f * (modelData.boundsMax - modelData.boundsMin));
    dynamicBoundsRadius_ = boundsRadius_;
    textures_.reserve(modelData.textures.size());
    textureFallbacks_.reserve(modelData.textures.size());
    for (const auto& textureData : modelData.textures) {
        TextureLoadResult loaded = textureCache.load(textureData);
        if (loaded.usedFallback) {
            ++fallbackTextureCount_;
        } else {
            ++loadedTextureCount_;
            textureMemoryBytes_ += loaded.texture->estimatedBytes();
        }
        if (!loaded.warning.empty()) {
            warnings.push_back(TextureUploadWarning{
                textureData.name.empty() ? textureData.cacheKey : textureData.name,
                std::move(loaded.warning)
            });
        }
        textures_.push_back(std::move(loaded.texture));
        textureFallbacks_.push_back(loaded.usedFallback);
    }

    materials_.reserve(modelData.materials.size());
    for (const auto& materialData : modelData.materials) {
        GpuMaterial material;
        material.baseColorFactor = materialData.baseColorFactor;
        material.metallicFactor = std::clamp(materialData.metallicFactor, 0.0f, 1.0f);
        material.roughnessFactor = std::clamp(materialData.roughnessFactor, 0.04f, 1.0f);
        material.transmissionFactor = std::clamp(materialData.transmissionFactor, 0.0f, 1.0f);
        material.indexOfRefraction = std::max(materialData.indexOfRefraction, 1.0f);
        material.dispersion = std::max(materialData.dispersion, 0.0f);
        material.thicknessFactor = std::max(materialData.thicknessFactor, 0.0f);
        material.attenuationColor = glm::clamp(
            materialData.attenuationColor,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        material.attenuationDistance = materialData.attenuationDistance > 0.0f
            ? materialData.attenuationDistance
            : std::numeric_limits<float>::infinity();
        material.alphaMode = materialData.alphaMode;
        material.alphaCutoff = std::max(materialData.alphaCutoff, 0.0f);
        material.doubleSided = materialData.doubleSided;
        if (materialData.baseColorTextureIndex >= 0
            && static_cast<std::size_t>(materialData.baseColorTextureIndex) < textures_.size()) {
            material.baseColorTexture = textures_[static_cast<std::size_t>(materialData.baseColorTextureIndex)];
        } else {
            material.baseColorTexture = whiteTexture_;
        }
        if (materialData.normalTextureIndex >= 0
            && static_cast<std::size_t>(materialData.normalTextureIndex) < textures_.size()
            && !textureFallbacks_[static_cast<std::size_t>(materialData.normalTextureIndex)]) {
            material.normalTexture = textures_[static_cast<std::size_t>(materialData.normalTextureIndex)];
            material.hasNormalTexture = true;
        } else {
            material.normalTexture = flatNormalTexture_;
        }
        if (materialData.metallicRoughnessTextureIndex >= 0
            && static_cast<std::size_t>(materialData.metallicRoughnessTextureIndex) < textures_.size()
            && !textureFallbacks_[static_cast<std::size_t>(materialData.metallicRoughnessTextureIndex)]) {
            material.metallicRoughnessTexture = textures_[static_cast<std::size_t>(materialData.metallicRoughnessTextureIndex)];
            material.hasMetallicRoughnessTexture = true;
        } else {
            material.metallicRoughnessTexture = linearWhiteTexture_;
        }
        if (materialData.thicknessTextureIndex >= 0
            && static_cast<std::size_t>(materialData.thicknessTextureIndex) < textures_.size()
            && !textureFallbacks_[static_cast<std::size_t>(materialData.thicknessTextureIndex)]) {
            material.thicknessTexture = textures_[static_cast<std::size_t>(materialData.thicknessTextureIndex)];
            material.hasThicknessTexture = true;
        } else {
            material.thicknessTexture = linearWhiteTexture_;
        }
        materials_.push_back(std::move(material));
    }

    meshes_.reserve(modelData.meshes.size());
    meshSkinJoints_.reserve(modelData.meshes.size());
    meshVertexTransformBaked_.reserve(modelData.meshes.size());
    for (const auto& meshData : modelData.meshes) {
        auto mesh = std::make_unique<Mesh>(meshData);
        submeshCount_ += mesh->submeshCount();
        vertexCount_ += mesh->vertexCount();
        triangleCount_ += mesh->triangleCount();
        meshes_.push_back(std::move(mesh));
        meshSkinJoints_.push_back(meshData.skinJoints);
        meshVertexTransformBaked_.push_back(meshData.vertexTransformBaked);
        jointCount_ += meshData.skinJoints.size();
    }

    skeletonNodes_ = modelData.skeletonNodes;
    animations_ = modelData.animations;
    nodeGlobalTransforms_.resize(skeletonNodes_.size(), glm::mat4(1.0f));
    previousNodeGlobalTransforms_.resize(skeletonNodes_.size(), glm::mat4(1.0f));
    updateAnimation(false, 0U, 0.0f);
    bindNodeGlobalTransforms_ = nodeGlobalTransforms_;

    const auto appendMeshCommands = [&](std::size_t meshIndex, const glm::mat4& nodeTransform) {
        if (meshIndex >= meshes_.size()) return;
        const Mesh& mesh = *meshes_[meshIndex];
        const glm::mat4 effectiveNodeTransform = meshVertexTransformBaked_[meshIndex]
            ? glm::mat4(1.0f)
            : nodeTransform;
        for (std::size_t submeshIndex = 0; submeshIndex < mesh.submeshCount(); ++submeshIndex) {
            const std::int32_t materialIndex = mesh.submeshMaterialIndex(submeshIndex);
            const bool isBlended = materialIndex >= 0
                && static_cast<std::size_t>(materialIndex) < materials_.size()
                && (materials_[static_cast<std::size_t>(materialIndex)].alphaMode == MaterialAlphaMode::Blend
                    || materials_[static_cast<std::size_t>(materialIndex)].transmissionFactor > 0.0f);
            DrawCommand command{
                meshIndex,
                submeshIndex,
                materialIndex,
                glm::vec3(
                    effectiveNodeTransform
                    * glm::vec4(mesh.submeshCenter(submeshIndex), 1.0f)
                ),
                effectiveNodeTransform
            };
            (isBlended ? transparentDrawCommands_ : opaqueDrawCommands_).push_back(command);
            if (materialIndex >= 0
                && static_cast<std::size_t>(materialIndex) < materials_.size()
                && materials_[static_cast<std::size_t>(materialIndex)].transmissionFactor > 0.0f) {
                transmissiveDrawCommands_.push_back(command);
            }
        }
    };
    std::function<void(const ModelNodeData&, const glm::mat4&)> appendNodeCommands;
    appendNodeCommands = [&](const ModelNodeData& node, const glm::mat4& parentTransform) {
        const glm::mat4 nodeTransform = parentTransform * node.localTransform;
        for (const std::uint32_t meshIndex : node.meshIndices) {
            appendMeshCommands(static_cast<std::size_t>(meshIndex), nodeTransform);
        }
        for (const ModelNodeData& child : node.children) {
            appendNodeCommands(child, nodeTransform);
        }
    };
    appendNodeCommands(modelData.rootNode, glm::mat4(1.0f));
    if (opaqueDrawCommands_.empty() && transparentDrawCommands_.empty()) {
        for (std::size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex) {
            appendMeshCommands(meshIndex, glm::mat4(1.0f));
        }
    }
}

GpuModel::~GpuModel() = default;

const GpuModel::GpuMaterial* GpuModel::bindMaterial(
    const Shader& shader,
    const glm::vec3& linearTint,
    std::int32_t materialIndex
) const {
    const GpuMaterial* material = nullptr;
    if (materialIndex >= 0 && static_cast<std::size_t>(materialIndex) < materials_.size()) {
        material = &materials_[static_cast<std::size_t>(materialIndex)];
    }
    const glm::vec4 baseColor = material == nullptr
        ? glm::vec4(linearTint, 1.0f)
        : glm::vec4(linearTint * glm::vec3(material->baseColorFactor), material->baseColorFactor.a);
    shader.setVec4("uBaseColor", baseColor);
    const auto& texture = material == nullptr ? whiteTexture_ : material->baseColorTexture;
    texture->bind(0U);
    const auto& normalTexture = material == nullptr ? flatNormalTexture_ : material->normalTexture;
    normalTexture->bind(1U);
    shader.setBool("uHasNormalTexture", material != nullptr && material->hasNormalTexture);
    const auto& metallicRoughnessTexture = material == nullptr
        ? linearWhiteTexture_
        : material->metallicRoughnessTexture;
    metallicRoughnessTexture->bind(2U);
    shader.setBool(
        "uHasMetallicRoughnessTexture",
        material != nullptr && material->hasMetallicRoughnessTexture
    );
    const auto& thicknessTexture = material == nullptr
        ? linearWhiteTexture_
        : material->thicknessTexture;
    thicknessTexture->bind(7U);
    shader.setBool("uHasThicknessTexture", material != nullptr && material->hasThicknessTexture);
    shader.setFloat("uMetallicFactor", material == nullptr ? 0.0f : material->metallicFactor);
    shader.setFloat("uRoughnessFactor", material == nullptr ? 1.0f : material->roughnessFactor);
    shader.setFloat("uTransmissionFactor", material == nullptr ? 0.0f : material->transmissionFactor);
    shader.setFloat("uIndexOfRefraction", material == nullptr ? 1.5f : material->indexOfRefraction);
    shader.setFloat("uMaterialDispersion", material == nullptr ? 0.0f : material->dispersion);
    shader.setFloat("uThicknessFactor", material == nullptr ? 0.0f : material->thicknessFactor);
    shader.setVec3(
        "uAttenuationColor",
        material == nullptr ? glm::vec3(1.0f) : material->attenuationColor
    );
    shader.setFloat(
        "uAttenuationDistance",
        material == nullptr || !std::isfinite(material->attenuationDistance)
            ? 1.0e20f
            : material->attenuationDistance
    );
    shader.setInt(
        "uAlphaMode",
        material == nullptr ? 0 : static_cast<int>(material->alphaMode)
    );
    shader.setFloat("uAlphaCutoff", material == nullptr ? 0.5f : material->alphaCutoff);
    shader.setBool("uDoubleSided", material != nullptr && material->doubleSided);
    return material;
}

void GpuModel::applyCullState(const GpuMaterial* material, bool cullBackFaces) const {
    const bool isVolumeBoundary = material != nullptr
        && material->transmissionFactor > 0.0f
        && material->thicknessFactor > 0.0f;
    if (isVolumeBoundary) {
        // Glass-2B selects the nearest volume surface from the front-depth
        // texture in the fragment shader, independent of mesh winding.
        glDisable(GL_CULL_FACE);
    } else if (cullBackFaces && (material == nullptr || !material->doubleSided)) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

bool GpuModel::commandVisible(const DrawCommand& command, const glm::mat4* modelToClip,
                              RasterVisibilityStatistics* statistics) const {
    const auto& mesh = *meshes_[command.meshIndex];
    // Skinning can move vertices outside bind bounds. Custom shader deformation
    // callers likewise leave modelToClip null, preserving their complete draws.
    const bool visible = modelToClip == nullptr || !meshSkinJoints_[command.meshIndex].empty()
        || intersectsClipBox(mesh.submeshBounds(command.submeshIndex), *modelToClip * command.nodeTransform);
    if (statistics) {
        ++statistics->submittedDraws;
        const auto triangles = mesh.submeshTriangleCount(command.submeshIndex);
        statistics->submittedTriangles += triangles;
        if (visible) statistics->drawnTriangles += triangles;
        else ++statistics->culledDraws;
    }
    return visible;
}

std::size_t GpuModel::drawOpaque(const Shader& shader, const glm::vec3& tint, bool cullBackFaces,
                                const glm::mat4* modelToClip, RasterVisibilityStatistics* statistics) const {
    shader.setBool("uInstanced", false);
    const glm::vec3 linearTint = srgbToLinear(tint);
    bindMaterialSamplers(shader);
    std::size_t drawn = 0;
    for (const DrawCommand& command : opaqueDrawCommands_) {
        if (!commandVisible(command, modelToClip, statistics)) continue;
        ++drawn;
        bindNodeTransform(shader, command);
        bindSkinning(shader, command.meshIndex);
        const GpuMaterial* material = bindMaterial(shader, linearTint, command.materialIndex);
        applyCullState(material, cullBackFaces);
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
    return drawn;
}

void GpuModel::drawOpaqueInstanced(
    const Shader& shader,
    const glm::vec3& tint,
    const std::vector<glm::mat4>& modelMatrices,
    std::size_t lodLevel,
    bool cullBackFaces
) const {
    if (modelMatrices.empty()) return;
    shader.setBool("uInstanced", true);
    const glm::vec3 linearTint = srgbToLinear(tint);
    bindMaterialSamplers(shader);
    for (const DrawCommand& command : opaqueDrawCommands_) {
        bindNodeTransform(shader, command);
        bindSkinning(shader, command.meshIndex);
        const GpuMaterial* material = bindMaterial(shader, linearTint, command.materialIndex);
        applyCullState(material, cullBackFaces);
        meshes_[command.meshIndex]->drawSubmeshInstanced(
            command.submeshIndex,
            lodLevel,
            modelMatrices
        );
    }
    shader.setBool("uInstanced", false);
}

std::size_t GpuModel::lodTriangleCount(std::size_t lodLevel) const {
    std::size_t count = 0U;
    for (const auto& mesh : meshes_) count += mesh->lodTriangleCount(lodLevel);
    return count;
}

void GpuModel::drawTransparentSubmesh(
    const Shader& shader,
    const glm::vec3& tint,
    std::size_t transparentSubmeshIndex,
    bool cullBackFaces
) const {
    if (transparentSubmeshIndex >= transparentDrawCommands_.size()) {
        throw std::out_of_range("Transparent submesh index is out of range");
    }
    const DrawCommand& command = transparentDrawCommands_[transparentSubmeshIndex];
    bindNodeTransform(shader, command);
    bindSkinning(shader, command.meshIndex);
    const glm::vec3 linearTint = srgbToLinear(tint);
    bindMaterialSamplers(shader);
    const GpuMaterial* material = bindMaterial(shader, linearTint, command.materialIndex);
    applyCullState(material, cullBackFaces);
    meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
}

const glm::vec3& GpuModel::transparentSubmeshCenter(std::size_t transparentSubmeshIndex) const {
    if (transparentSubmeshIndex >= transparentDrawCommands_.size()) {
        throw std::out_of_range("Transparent submesh index is out of range");
    }
    return transparentDrawCommands_[transparentSubmeshIndex].localCenter;
}

bool GpuModel::transparentSubmeshIsTransmissive(
    std::size_t transparentSubmeshIndex
) const {
    if (transparentSubmeshIndex >= transparentDrawCommands_.size()) {
        throw std::out_of_range("Transparent submesh index is out of range");
    }
    const std::int32_t materialIndex =
        transparentDrawCommands_[transparentSubmeshIndex].materialIndex;
    return materialIndex >= 0
        && static_cast<std::size_t>(materialIndex) < materials_.size()
        && materials_[static_cast<std::size_t>(materialIndex)].transmissionFactor > 0.0f;
}

std::size_t GpuModel::drawDepth(const Shader& shader, const glm::mat4* modelToClip,
                               RasterVisibilityStatistics* statistics) const {
    std::size_t drawn = 0;
    for (const DrawCommand& command : opaqueDrawCommands_) {
        if (!commandVisible(command, modelToClip, statistics)) continue;
        ++drawn;
        bindNodeTransform(shader, command);
        bindSkinning(shader, command.meshIndex);
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
    return drawn;
}

void GpuModel::drawTransmissiveDepth(const Shader& shader) const {
    for (const DrawCommand& command : transmissiveDrawCommands_) {
        bindNodeTransform(shader, command);
        bindSkinning(shader, command.meshIndex);
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
}

void GpuModel::drawTransmissive(const Shader& shader, const glm::vec3& tint) const {
    const glm::vec3 linearTint = srgbToLinear(tint);
    bindMaterialSamplers(shader);
    for (const DrawCommand& command : transmissiveDrawCommands_) {
        bindNodeTransform(shader, command);
        bindSkinning(shader, command.meshIndex);
        bindMaterial(shader, linearTint, command.materialIndex);
        glDisable(GL_CULL_FACE);
        meshes_[command.meshIndex]->drawSubmesh(command.submeshIndex);
    }
}

void GpuModel::bindNodeTransform(const Shader& shader, const DrawCommand& command) const {
    shader.setMat4("uNodeTransform", command.nodeTransform);
}

const std::string& GpuModel::animationName(std::size_t index) const {
    if (index >= animations_.size()) throw std::out_of_range("Animation index is out of range");
    return animations_[index].name;
}

float GpuModel::animationDuration(std::size_t index) const {
    if (index >= animations_.size()) return 0.0f;
    return animations_[index].durationSeconds;
}

void GpuModel::updateAnimation(bool enabled, std::size_t clipIndex, float timeSeconds) {
    previousSkinningValid_ = animationPoseInitialized_ && !nodeGlobalTransforms_.empty();
    if (previousSkinningValid_) previousNodeGlobalTransforms_ = nodeGlobalTransforms_;
    const AnimationClipData* clip = enabled && clipIndex < animations_.size()
        ? &animations_[clipIndex]
        : nullptr;
    float sampleTime = std::max(timeSeconds, 0.0f);
    if (clip != nullptr && clip->durationSeconds > 1.0e-6f) {
        sampleTime = std::fmod(sampleTime, clip->durationSeconds);
    }
    for (std::size_t nodeIndex = 0; nodeIndex < skeletonNodes_.size(); ++nodeIndex) {
        const SkeletonNodeData& node = skeletonNodes_[nodeIndex];
        glm::vec3 translation = node.bindTranslation;
        glm::quat rotation = node.bindRotation;
        glm::vec3 scale = node.bindScale;
        if (clip != nullptr) {
            const auto channel = std::find_if(
                clip->channels.begin(), clip->channels.end(),
                [&](const AnimationChannelData& candidate) {
                    return candidate.nodeIndex == nodeIndex;
                }
            );
            if (channel != clip->channels.end()) {
                translation = sampleVector(channel->translations, sampleTime, translation);
                rotation = sampleRotation(channel->rotations, sampleTime, rotation);
                scale = sampleVector(channel->scales, sampleTime, scale);
            }
        }
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), translation)
            * glm::mat4_cast(rotation)
            * glm::scale(glm::mat4(1.0f), scale);
        nodeGlobalTransforms_[nodeIndex] = node.parentIndex >= 0
            ? nodeGlobalTransforms_[static_cast<std::size_t>(node.parentIndex)] * local
            : local;
    }
    skinningMotionValid_ = false;
    if (previousSkinningValid_) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodeGlobalTransforms_.size(); ++nodeIndex) {
            for (int column = 0; column < 4 && !skinningMotionValid_; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (std::abs(
                        nodeGlobalTransforms_[nodeIndex][column][row]
                        - previousNodeGlobalTransforms_[nodeIndex][column][row]
                    ) > 1.0e-6f) {
                        skinningMotionValid_ = true;
                        break;
                    }
                }
            }
            if (skinningMotionValid_) break;
        }
    }
    dynamicBoundsRadius_ = boundsRadius_;
    if (bindNodeGlobalTransforms_.size() == nodeGlobalTransforms_.size()) {
        float maximumJointDisplacement = 0.0f;
        for (std::size_t nodeIndex = 0; nodeIndex < nodeGlobalTransforms_.size(); ++nodeIndex) {
            maximumJointDisplacement = std::max(
                maximumJointDisplacement,
                glm::length(
                    glm::vec3(nodeGlobalTransforms_[nodeIndex][3])
                    - glm::vec3(bindNodeGlobalTransforms_[nodeIndex][3])
                )
            );
        }
        // Translation-only growth misses vertices swept by a rotating joint. One
        // additional bind-pose radius conservatively covers that rotational arc.
        dynamicBoundsRadius_ += boundsRadius_ + maximumJointDisplacement;
    }
    animationPoseInitialized_ = true;
}

void GpuModel::bindSkinning(const Shader& shader, std::size_t meshIndex) const {
    if (meshIndex >= meshSkinJoints_.size() || meshSkinJoints_[meshIndex].empty()) {
        shader.setBool("uSkinningEnabled", false);
        shader.setBool("uPreviousSkinningValid", false);
        shader.setInt("uSkinningDebugView", 0);
        return;
    }
    const auto& joints = meshSkinJoints_[meshIndex];
    std::array<glm::mat4, 64> palette{};
    std::array<glm::mat4, 64> previousPalette{};
    for (std::size_t index = 0; index < joints.size() && index < palette.size(); ++index) {
        const SkinJointData& joint = joints[index];
        palette[index] = joint.nodeIndex < nodeGlobalTransforms_.size()
            ? nodeGlobalTransforms_[joint.nodeIndex] * joint.inverseBindMatrix
            : glm::mat4(1.0f);
        previousPalette[index] = previousSkinningValid_
            && joint.nodeIndex < previousNodeGlobalTransforms_.size()
            ? previousNodeGlobalTransforms_[joint.nodeIndex] * joint.inverseBindMatrix
            : palette[index];
    }
    shader.setBool("uSkinningEnabled", true);
    shader.setInt("uSkinningDebugView", skinningDebugView_);
    shader.setMat4Array("uJointMatrices[0]", palette.data(), std::min(joints.size(), palette.size()));
    shader.setBool("uPreviousSkinningValid", previousSkinningValid_);
    shader.setMat4Array(
        "uPreviousJointMatrices[0]",
        previousPalette.data(),
        std::min(joints.size(), previousPalette.size())
    );
}
