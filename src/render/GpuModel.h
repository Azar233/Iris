#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "asset/ModelData.h"
#include "render/SceneDrawList.h"

class Mesh;
class Shader;
class Texture2D;
class TextureCache;

struct TextureUploadWarning {
    std::string textureName;
    std::string message;
};

class GpuModel {
public:
    GpuModel(
        ModelData data,
        TextureCache& textureCache,
        std::vector<TextureUploadWarning>& warnings
    );
    ~GpuModel();

    GpuModel(const GpuModel&) = delete;
    GpuModel& operator=(const GpuModel&) = delete;

    std::size_t drawOpaque(const Shader& shader, const glm::vec3& tint, bool cullBackFaces,
        const glm::mat4* modelToClip = nullptr, RasterVisibilityStatistics* statistics = nullptr) const;
    void drawOpaqueInstanced(
        const Shader& shader,
        const glm::vec3& tint,
        const std::vector<glm::mat4>& modelMatrices,
        std::size_t lodLevel,
        bool cullBackFaces
    ) const;
    void drawTransparentSubmesh(
        const Shader& shader,
        const glm::vec3& tint,
        std::size_t transparentSubmeshIndex,
        bool cullBackFaces
    ) const;
    std::size_t drawDepth(const Shader& shader, const glm::mat4* modelToClip = nullptr,
        RasterVisibilityStatistics* statistics = nullptr) const;
    void drawTransmissiveDepth(const Shader& shader) const;
    void drawTransmissive(const Shader& shader, const glm::vec3& tint) const;
    const glm::vec3& transparentSubmeshCenter(std::size_t transparentSubmeshIndex) const;

    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t submeshCount() const { return submeshCount_; }
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t triangleCount() const { return triangleCount_; }
    std::size_t lodTriangleCount(std::size_t lodLevel) const;
    const glm::vec3& boundsCenter() const { return boundsCenter_; }
    float boundsRadius() const { return dynamicBoundsRadius_; }
    std::size_t materialCount() const { return materials_.size(); }
    std::size_t textureCount() const { return textures_.size(); }
    std::size_t opaqueSubmeshCount() const { return opaqueDrawCommands_.size(); }
    std::size_t transparentSubmeshCount() const { return transparentDrawCommands_.size(); }
    std::size_t transmissiveSubmeshCount() const { return transmissiveDrawCommands_.size(); }
    bool transparentSubmeshIsTransmissive(std::size_t transparentSubmeshIndex) const;
    std::size_t loadedTextureCount() const { return loadedTextureCount_; }
    std::size_t fallbackTextureCount() const { return fallbackTextureCount_; }
    std::size_t textureMemoryBytes() const { return textureMemoryBytes_; }
    bool hasSkinning() const { return jointCount_ > 0U; }
    std::size_t jointCount() const { return jointCount_; }
    std::size_t animationCount() const { return animations_.size(); }
    const std::string& animationName(std::size_t index) const;
    float animationDuration(std::size_t index) const;
    void updateAnimation(bool enabled, std::size_t clipIndex, float timeSeconds);
    void setSkinningDebugView(int view) { skinningDebugView_ = view; }
    bool hasSkinningMotion() const { return skinningMotionValid_; }
    const std::shared_ptr<const ModelData>& sourceData() const { return sourceData_; }

private:
    struct GpuMaterial {
        glm::vec4 baseColorFactor{1.0f};
        std::shared_ptr<Texture2D> baseColorTexture;
        std::shared_ptr<Texture2D> normalTexture;
        std::shared_ptr<Texture2D> metallicRoughnessTexture;
        std::shared_ptr<Texture2D> thicknessTexture;
        bool hasNormalTexture{false};
        bool hasMetallicRoughnessTexture{false};
        bool hasThicknessTexture{false};
        float metallicFactor{0.0f};
        float roughnessFactor{1.0f};
        float transmissionFactor{0.0f};
        float indexOfRefraction{1.5f};
        float dispersion{0.0f};
        float thicknessFactor{0.0f};
        glm::vec3 attenuationColor{1.0f};
        float attenuationDistance{std::numeric_limits<float>::infinity()};
        MaterialAlphaMode alphaMode{MaterialAlphaMode::Opaque};
        float alphaCutoff{0.5f};
        bool doubleSided{false};
    };

    struct DrawCommand {
        std::size_t meshIndex{0};
        std::size_t submeshIndex{0};
        std::int32_t materialIndex{-1};
        glm::vec3 localCenter{0.0f};
        glm::mat4 nodeTransform{1.0f};
    };

    bool commandVisible(const DrawCommand&, const glm::mat4*, RasterVisibilityStatistics*) const;
    const GpuMaterial* bindMaterial(
        const Shader& shader,
        const glm::vec3& linearTint,
        std::int32_t materialIndex
    ) const;
    void applyCullState(const GpuMaterial* material, bool cullBackFaces) const;
    void bindSkinning(const Shader& shader, std::size_t meshIndex) const;
    void bindNodeTransform(const Shader& shader, const DrawCommand& command) const;

    std::shared_ptr<const ModelData> sourceData_;
    std::vector<std::unique_ptr<Mesh>> meshes_;
    std::vector<std::vector<SkinJointData>> meshSkinJoints_;
    std::vector<bool> meshVertexTransformBaked_;
    std::vector<SkeletonNodeData> skeletonNodes_;
    std::vector<AnimationClipData> animations_;
    std::vector<glm::mat4> nodeGlobalTransforms_;
    std::vector<glm::mat4> previousNodeGlobalTransforms_;
    std::vector<glm::mat4> bindNodeGlobalTransforms_;
    std::vector<GpuMaterial> materials_;
    std::vector<std::shared_ptr<Texture2D>> textures_;
    std::vector<bool> textureFallbacks_;
    std::shared_ptr<Texture2D> whiteTexture_;
    std::shared_ptr<Texture2D> flatNormalTexture_;
    std::shared_ptr<Texture2D> linearWhiteTexture_;
    std::vector<DrawCommand> opaqueDrawCommands_;
    std::vector<DrawCommand> transparentDrawCommands_;
    std::vector<DrawCommand> transmissiveDrawCommands_;
    std::size_t submeshCount_{0};
    std::size_t vertexCount_{0};
    std::size_t triangleCount_{0};
    std::size_t loadedTextureCount_{0};
    std::size_t fallbackTextureCount_{0};
    std::size_t textureMemoryBytes_{0};
    std::size_t jointCount_{0};
    int skinningDebugView_{0};
    glm::vec3 boundsCenter_{0.0f};
    float boundsRadius_{0.0f};
    float dynamicBoundsRadius_{0.0f};
    bool animationPoseInitialized_{false};
    bool previousSkinningValid_{false};
    bool skinningMotionValid_{false};
};
