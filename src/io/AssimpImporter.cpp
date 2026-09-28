#include "asset/InputManifest.h"
#include <assimp/DefaultIOSystem.h>
#include "io/AssimpImporter.h"
#include "io/GltfMaterialExtensions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

namespace {
class CaptureIO final : public Assimp::IOSystem {
public:
    bool Exists(const char* path) const override {return io_.Exists(path);}
    char getOsSeparator() const override {return io_.getOsSeparator();}
    Assimp::IOStream* Open(const char* path,const char* mode="rb") override {
        capture::recordInput(path);
        return io_.Open(path,mode);
    }
    void Close(Assimp::IOStream* stream) override {io_.Close(stream);}
private:
    Assimp::DefaultIOSystem io_;
};


void addWarning(
    std::string& warnings,
    std::vector<ModelDiagnostic>& diagnostics,
    ModelDiagnosticScope scope,
    std::string context,
    std::string message
) {
    warnings += message + "\n";
    diagnostics.push_back(ModelDiagnostic{
        scope,
        ModelDiagnosticSeverity::Warning,
        std::move(context),
        std::move(message)
    });
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

glm::vec3 toVec3(const aiVector3D& value) {
    return {value.x, value.y, value.z};
}

glm::mat4 toMat4(const aiMatrix4x4& value) {
    return {
        value.a1, value.b1, value.c1, value.d1,
        value.a2, value.b2, value.c2, value.d2,
        value.a3, value.b3, value.c3, value.d3,
        value.a4, value.b4, value.c4, value.d4
    };
}

glm::quat toQuat(const aiQuaternion& value) {
    return glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
}

void addVertexInfluence(Vertex& vertex, std::uint32_t jointIndex, float weight) {
    if (!(weight > 0.0f)) return;
    int destination = -1;
    for (int component = 0; component < 4; ++component) {
        if (vertex.jointWeights[component] <= 0.0f) {
            destination = component;
            break;
        }
    }
    if (destination < 0) {
        destination = 0;
        for (int component = 1; component < 4; ++component) {
            if (vertex.jointWeights[component] < vertex.jointWeights[destination]) {
                destination = component;
            }
        }
        if (weight <= vertex.jointWeights[destination]) return;
    }
    vertex.jointIndices[destination] = jointIndex;
    vertex.jointWeights[destination] = weight;
}

std::vector<std::uint8_t> decodeBase64(std::string_view input) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> output;
    output.reserve((input.size() * 3U) / 4U);
    unsigned int accumulator = 0U;
    int bits = 0;
    for (const char character : input) {
        if (character == '=') {
            break;
        }
        const std::size_t value = alphabet.find(character);
        if (value == std::string_view::npos) {
            continue;
        }
        accumulator = (accumulator << 6U) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xffU));
        }
    }
    return output;
}

std::vector<std::uint8_t> decodeDataUri(const std::string& uri) {
    const std::size_t comma = uri.find(',');
    if (comma == std::string::npos) {
        return {};
    }
    const std::string_view metadata(uri.data(), comma);
    const std::string_view payload(uri.data() + comma + 1U, uri.size() - comma - 1U);
    if (metadata.find(";base64") != std::string_view::npos) {
        return decodeBase64(payload);
    }

    auto hexValue = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> output;
    output.reserve(payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (payload[i] == '%' && i + 2U < payload.size()) {
            const int high = hexValue(payload[i + 1U]);
            const int low = hexValue(payload[i + 2U]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<std::uint8_t>((high << 4) | low));
                i += 2U;
                continue;
            }
        }
        output.push_back(static_cast<std::uint8_t>(payload[i]));
    }
    return output;
}

std::int32_t appendTextureReference(
    const aiScene& scene,
    ModelData& model,
    std::unordered_map<std::string, std::int32_t>& textureIndices,
    const aiString& reference,
    const std::filesystem::path& sourceDirectory,
    std::string& warnings,
    std::vector<ModelDiagnostic>& diagnostics,
    const std::string& materialName,
    bool srgb
) {
    const std::string value = reference.C_Str();
    if (value.empty()) {
        return -1;
    }

    TextureData texture;
    const auto embeddedResult = scene.GetEmbeddedTextureAndIndex(value.c_str());
    if (embeddedResult.first != nullptr) {
        const aiTexture& embedded = *embeddedResult.first;
        texture.name = embedded.mFilename.length > 0U ? embedded.mFilename.C_Str() : value;
        texture.cacheKey = model.sourcePath.generic_string() + "::embedded::"
                         + std::to_string(embeddedResult.second)
                         + (srgb ? "::srgb" : "::linear");
        if (embedded.mHeight == 0U) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(embedded.pcData);
            texture.encodedData.assign(bytes, bytes + embedded.mWidth);
        } else {
            texture.width = embedded.mWidth;
            texture.height = embedded.mHeight;
            texture.rgbaPixels.resize(
                static_cast<std::size_t>(embedded.mWidth) * embedded.mHeight * 4U
            );
            for (std::size_t i = 0; i < static_cast<std::size_t>(embedded.mWidth) * embedded.mHeight; ++i) {
                const aiTexel& pixel = embedded.pcData[i];
                texture.rgbaPixels[i * 4U] = pixel.r;
                texture.rgbaPixels[i * 4U + 1U] = pixel.g;
                texture.rgbaPixels[i * 4U + 2U] = pixel.b;
                texture.rgbaPixels[i * 4U + 3U] = pixel.a;
            }
        }
    } else if (value.rfind("data:", 0U) == 0U) {
        texture.name = "Data URI image";
        texture.cacheKey = model.sourcePath.generic_string() + "::data-uri::"
                         + std::to_string(std::hash<std::string>{}(value))
                         + (srgb ? "::srgb" : "::linear");
        texture.encodedData = decodeDataUri(value);
        if (texture.encodedData.empty()) {
            addWarning(
                warnings,
                diagnostics,
                ModelDiagnosticScope::Texture,
                materialName,
                "Could not decode texture Data URI in " + model.sourcePath.string()
            );
        }
    } else {
        texture.sourcePath = std::filesystem::absolute(
            sourceDirectory / std::filesystem::path(value)
        ).lexically_normal();
        texture.name = texture.sourcePath.filename().string();
        texture.cacheKey = texture.sourcePath.generic_string() + (srgb ? "::srgb" : "::linear");
    }
    texture.srgb = srgb;

    const auto found = textureIndices.find(texture.cacheKey);
    if (found != textureIndices.end()) {
        return found->second;
    }
    const auto index = static_cast<std::int32_t>(model.textures.size());
    textureIndices.emplace(texture.cacheKey, index);
    model.textures.push_back(std::move(texture));
    return index;
}

std::int32_t materialTexture(
    const aiMaterial& material,
    aiTextureType preferredType,
    aiTextureType fallbackType,
    const aiScene& scene,
    ModelData& model,
    std::unordered_map<std::string, std::int32_t>& textureIndices,
    const std::filesystem::path& sourceDirectory,
    std::string& warnings,
    std::vector<ModelDiagnostic>& diagnostics,
    const std::string& materialName,
    bool srgb
) {
    aiString reference;
    if (material.GetTexture(preferredType, 0, &reference) == AI_SUCCESS
        || material.GetTexture(fallbackType, 0, &reference) == AI_SUCCESS) {
        return appendTextureReference(
            scene,
            model,
            textureIndices,
            reference,
            sourceDirectory,
            warnings,
            diagnostics,
            materialName,
            srgb
        );
    }
    return -1;
}

std::int32_t materialTextureAt(
    const aiMaterial& material,
    aiTextureType type,
    unsigned int textureIndex,
    const aiScene& scene,
    ModelData& model,
    std::unordered_map<std::string, std::int32_t>& textureIndices,
    const std::filesystem::path& sourceDirectory,
    std::string& warnings,
    std::vector<ModelDiagnostic>& diagnostics,
    const std::string& materialName,
    bool srgb
) {
    aiString reference;
    if (material.GetTexture(type, textureIndex, &reference) != AI_SUCCESS) {
        return -1;
    }
    return appendTextureReference(
        scene,
        model,
        textureIndices,
        reference,
        sourceDirectory,
        warnings,
        diagnostics,
        materialName,
        srgb
    );
}

struct ImportContext {
    const aiScene& scene;
    ModelData& model;
    std::string& warnings;
    std::vector<ModelDiagnostic>& diagnostics;
    const std::unordered_map<std::string, std::uint32_t>& nodeIndices;
    std::unordered_map<unsigned int, std::uint32_t> staticMeshIndices;

    std::optional<std::uint32_t> appendMesh(
        const aiMesh& source,
        const aiMatrix4x4& globalTransform,
        const std::string& nodeName
    ) {
        if (!source.HasPositions()) {
            addWarning(
                warnings,
                diagnostics,
                ModelDiagnosticScope::Mesh,
                source.mName.length > 0U ? source.mName.C_Str() : nodeName,
                "Skipped mesh without positions in node: " + nodeName
            );
            return std::nullopt;
        }

        MeshData mesh;
        mesh.name = source.mName.length > 0U ? source.mName.C_Str() : nodeName;
        mesh.vertexTransformBaked = source.HasBones();
        mesh.vertices.reserve(source.mNumVertices);

        const aiMatrix4x4 vertexTransform = mesh.vertexTransformBaked
            ? globalTransform
            : aiMatrix4x4{};
        aiMatrix3x3 normalMatrix(vertexTransform);
        normalMatrix.Inverse().Transpose();
        const bool mirrored = aiMatrix3x3(vertexTransform).Determinant() < 0.0f;

        for (unsigned int vertexIndex = 0; vertexIndex < source.mNumVertices; ++vertexIndex) {
            Vertex vertex;
            const aiVector3D transformedPosition = vertexTransform * source.mVertices[vertexIndex];
            vertex.position = toVec3(transformedPosition);

            if (source.HasNormals()) {
                const aiVector3D transformedNormal = normalMatrix * source.mNormals[vertexIndex];
                if (transformedNormal.SquareLength() > 1e-16f) {
                    vertex.normal = glm::normalize(toVec3(transformedNormal));
                }
            }
            if (source.HasTextureCoords(0)) {
                vertex.texCoord0 = {
                    source.mTextureCoords[0][vertexIndex].x,
                    source.mTextureCoords[0][vertexIndex].y
                };
            }
            if (source.HasTangentsAndBitangents()) {
                const aiVector3D transformedTangent = normalMatrix * source.mTangents[vertexIndex];
                const aiVector3D transformedBitangent = normalMatrix * source.mBitangents[vertexIndex];
                if (transformedTangent.SquareLength() > 1e-16f
                    && transformedBitangent.SquareLength() > 1e-16f) {
                    const glm::vec3 tangent = glm::normalize(toVec3(transformedTangent));
                    const glm::vec3 bitangent = glm::normalize(toVec3(transformedBitangent));
                    const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f
                        ? -1.0f
                        : 1.0f;
                    vertex.tangent = glm::vec4(tangent, handedness);
                }
            }

            mesh.boundsMin = glm::min(mesh.boundsMin, vertex.position);
            mesh.boundsMax = glm::max(mesh.boundsMax, vertex.position);
            mesh.vertices.push_back(vertex);
        }

        if (source.HasBones()) {
            const unsigned int boneCount = std::min(source.mNumBones, 64U);
            mesh.skinJoints.reserve(boneCount);
            const glm::mat4 inverseMeshTransform = glm::inverse(toMat4(globalTransform));
            for (unsigned int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                const aiBone& bone = *source.mBones[boneIndex];
                const auto node = nodeIndices.find(bone.mName.C_Str());
                if (node == nodeIndices.end()) {
                    addWarning(
                        warnings,
                        diagnostics,
                        ModelDiagnosticScope::Node,
                        bone.mName.C_Str(),
                        "Skipped skin joint without a matching scene node."
                    );
                    continue;
                }
                const std::uint32_t paletteIndex = static_cast<std::uint32_t>(
                    mesh.skinJoints.size()
                );
                mesh.skinJoints.push_back(SkinJointData{
                    node->second,
                    toMat4(bone.mOffsetMatrix) * inverseMeshTransform
                });
                for (unsigned int weightIndex = 0; weightIndex < bone.mNumWeights; ++weightIndex) {
                    const aiVertexWeight& influence = bone.mWeights[weightIndex];
                    if (influence.mVertexId < mesh.vertices.size()) {
                        addVertexInfluence(
                            mesh.vertices[influence.mVertexId],
                            paletteIndex,
                            influence.mWeight
                        );
                    }
                }
            }
            for (Vertex& vertex : mesh.vertices) {
                const float sum = vertex.jointWeights.x + vertex.jointWeights.y
                    + vertex.jointWeights.z + vertex.jointWeights.w;
                if (sum > 1.0e-6f) {
                    vertex.jointWeights /= sum;
                } else if (!mesh.skinJoints.empty()) {
                    vertex.jointIndices = glm::uvec4(0U);
                    vertex.jointWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }
            if (source.mNumBones > 64U) {
                addWarning(
                    warnings,
                    diagnostics,
                    ModelDiagnosticScope::Mesh,
                    mesh.name,
                    "Skin palette was truncated to the OpenGL 3.3 limit of 64 joints."
                );
            }
        }

        for (unsigned int faceIndex = 0; faceIndex < source.mNumFaces; ++faceIndex) {
            const aiFace& face = source.mFaces[faceIndex];
            if (face.mNumIndices != 3U) {
                continue;
            }
            mesh.indices.push_back(face.mIndices[0]);
            mesh.indices.push_back(face.mIndices[mirrored ? 2U : 1U]);
            mesh.indices.push_back(face.mIndices[mirrored ? 1U : 2U]);
        }
        if (mesh.indices.empty()) {
            addWarning(
                warnings,
                diagnostics,
                ModelDiagnosticScope::Mesh,
                mesh.name,
                "Skipped mesh without triangle faces."
            );
            return std::nullopt;
        }

        mesh.submeshes.push_back(SubmeshData{
            mesh.name,
            0U,
            static_cast<std::uint32_t>(mesh.indices.size()),
            source.mMaterialIndex < model.materials.size()
                ? static_cast<std::int32_t>(source.mMaterialIndex)
                : -1
        });
        const auto modelMeshIndex = static_cast<std::uint32_t>(model.meshes.size());
        model.meshes.push_back(std::move(mesh));
        return modelMeshIndex;
    }

    void extendModelBounds(const MeshData& mesh, const aiMatrix4x4& nodeTransform) {
        const std::array<glm::vec3, 8> corners{
            glm::vec3(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z),
            glm::vec3(mesh.boundsMax.x, mesh.boundsMin.y, mesh.boundsMin.z),
            glm::vec3(mesh.boundsMin.x, mesh.boundsMax.y, mesh.boundsMin.z),
            glm::vec3(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMin.z),
            glm::vec3(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMax.z),
            glm::vec3(mesh.boundsMax.x, mesh.boundsMin.y, mesh.boundsMax.z),
            glm::vec3(mesh.boundsMin.x, mesh.boundsMax.y, mesh.boundsMax.z),
            glm::vec3(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z)
        };
        const glm::mat4 transform = mesh.vertexTransformBaked
            ? glm::mat4(1.0f)
            : toMat4(nodeTransform);
        for (const glm::vec3& corner : corners) {
            const glm::vec3 transformed = glm::vec3(transform * glm::vec4(corner, 1.0f));
            model.boundsMin = glm::min(model.boundsMin, transformed);
            model.boundsMax = glm::max(model.boundsMax, transformed);
        }
    }

    ModelNodeData appendNode(const aiNode& source, const aiMatrix4x4& parentTransform) {
        ModelNodeData node;
        node.name = source.mName.length > 0U ? source.mName.C_Str() : "Node";
        node.localTransform = toMat4(source.mTransformation);
        const aiMatrix4x4 globalTransform = parentTransform * source.mTransformation;

        for (unsigned int meshIndex = 0; meshIndex < source.mNumMeshes; ++meshIndex) {
            const unsigned int sceneMeshIndex = source.mMeshes[meshIndex];
            if (sceneMeshIndex >= scene.mNumMeshes) {
                addWarning(
                    warnings,
                    diagnostics,
                    ModelDiagnosticScope::Node,
                    node.name,
                    "Skipped invalid mesh reference #" + std::to_string(sceneMeshIndex) + "."
                );
                continue;
            }
            const aiMesh& sceneMesh = *scene.mMeshes[sceneMeshIndex];
            std::optional<std::uint32_t> modelMeshIndex;
            const auto cached = staticMeshIndices.find(sceneMeshIndex);
            if (!sceneMesh.HasBones() && cached != staticMeshIndices.end()) {
                modelMeshIndex = cached->second;
            } else {
                modelMeshIndex = appendMesh(sceneMesh, globalTransform, node.name);
                if (modelMeshIndex.has_value() && !sceneMesh.HasBones()) {
                    staticMeshIndices.emplace(sceneMeshIndex, *modelMeshIndex);
                }
            }
            if (modelMeshIndex.has_value()) {
                node.meshIndices.push_back(*modelMeshIndex);
                extendModelBounds(model.meshes[*modelMeshIndex], globalTransform);
            }
        }

        node.children.reserve(source.mNumChildren);
        for (unsigned int childIndex = 0; childIndex < source.mNumChildren; ++childIndex) {
            node.children.push_back(appendNode(*source.mChildren[childIndex], globalTransform));
        }
        return node;
    }
};

} // namespace

bool AssimpImporter::supports(const std::filesystem::path& path) const {
    const std::string extension = lowercase(path.extension().string());
    return extension == ".dae" || extension == ".gltf" || extension == ".glb";
}

ModelImportResult AssimpImporter::load(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Model file does not exist: " + path.string());
    }

    Assimp::Importer importer;
    if(capture::activeManifest) importer.SetIOHandler(new CaptureIO());
    constexpr unsigned int flags =
        aiProcess_CalcTangentSpace
        | aiProcess_JoinIdenticalVertices
        | aiProcess_Triangulate
        | aiProcess_GenSmoothNormals
        | aiProcess_ImproveCacheLocality
        | aiProcess_SortByPType
        | aiProcess_ValidateDataStructure;
    const aiScene* scene = importer.ReadFile(path.string(), flags);
    if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0U) {
        throw std::runtime_error("Failed to import model with Assimp: " + std::string(importer.GetErrorString()));
    }

    ModelImportResult result;
    result.model.name = path.stem().string();
    result.model.sourcePath = std::filesystem::absolute(path).lexically_normal();
    const std::filesystem::path sourceDirectory = result.model.sourcePath.parent_path();
    std::unordered_map<std::string, std::int32_t> textureIndices;

    const std::vector<float> materialDispersion = loadGltfMaterialDispersion(path);
    result.model.materials.reserve(scene->mNumMaterials);
    for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        const aiMaterial& source = *scene->mMaterials[materialIndex];
        MaterialData material;
        if (materialIndex < materialDispersion.size()) {
            material.dispersion = materialDispersion[materialIndex];
        }
        aiString name;
        if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0U) {
            material.name = name.C_Str();
        }
        aiColor4D baseColor;
        aiColor3D emission(0.0f, 0.0f, 0.0f);
        if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emission) == AI_SUCCESS) {
            material.emissiveFactor = {emission.r, emission.g, emission.b};
        }
        if (aiGetMaterialColor(&source, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS
            || aiGetMaterialColor(&source, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == AI_SUCCESS) {
            material.baseColorFactor = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};
        }
        source.Get(AI_MATKEY_METALLIC_FACTOR, material.metallicFactor);
        source.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughnessFactor);
        source.Get(AI_MATKEY_TRANSMISSION_FACTOR, material.transmissionFactor);
        source.Get(AI_MATKEY_REFRACTI, material.indexOfRefraction);
        source.Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, material.thicknessFactor);
        aiColor3D attenuationColor(1.0f, 1.0f, 1.0f);
        if (source.Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, attenuationColor) == AI_SUCCESS) {
            material.attenuationColor = {
                attenuationColor.r,
                attenuationColor.g,
                attenuationColor.b
            };
        }
        source.Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, material.attenuationDistance);
        material.transmissionFactor = std::clamp(material.transmissionFactor, 0.0f, 1.0f);
        if (material.indexOfRefraction < 1.0f) {
            material.indexOfRefraction = 1.5f;
        }
        material.thicknessFactor = std::max(material.thicknessFactor, 0.0f);
        material.attenuationColor = glm::clamp(
            material.attenuationColor,
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        );
        if (!(material.attenuationDistance > 0.0f)) {
            material.attenuationDistance = std::numeric_limits<float>::infinity();
        }
        aiString alphaMode;
        if (source.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
            const std::string mode = lowercase(alphaMode.C_Str());
            if (mode == "mask") {
                material.alphaMode = MaterialAlphaMode::Mask;
            } else if (mode == "blend") {
                material.alphaMode = MaterialAlphaMode::Blend;
            }
        }
        source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.alphaCutoff);
        material.alphaCutoff = std::max(material.alphaCutoff, 0.0f);
        int doubleSided = 0;
        if (source.Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS) {
            material.doubleSided = doubleSided != 0;
        }
        material.baseColorTextureIndex = materialTexture(
            source,
            aiTextureType_BASE_COLOR,
            aiTextureType_DIFFUSE,
            *scene,
            result.model,
            textureIndices,
            sourceDirectory,
            result.warnings,
            result.diagnostics,
            material.name,
            true
        );
        material.normalTextureIndex = materialTexture(
            source,
            aiTextureType_NORMALS,
            aiTextureType_HEIGHT,
            *scene,
            result.model,
            textureIndices,
            sourceDirectory,
            result.warnings,
            result.diagnostics,
            material.name,
            false
        );
        material.metallicRoughnessTextureIndex = materialTexture(
            source,
            aiTextureType_GLTF_METALLIC_ROUGHNESS,
            aiTextureType_METALNESS,
            *scene,
            result.model,
            textureIndices,
            sourceDirectory,
            result.warnings,
            result.diagnostics,
            material.name,
            false
        );
        material.thicknessTextureIndex = materialTextureAt(
            source,
            aiTextureType_TRANSMISSION,
            1U,
            *scene,
            result.model,
            textureIndices,
            sourceDirectory,
            result.warnings,
            result.diagnostics,
            material.name,
            false
        );
        result.model.materials.push_back(std::move(material));
    }

    std::unordered_map<std::string, std::uint32_t> nodeIndices;
    std::function<void(const aiNode&, std::int32_t)> appendSkeletonNode;
    appendSkeletonNode = [&](const aiNode& source, std::int32_t parentIndex) {
        aiVector3D scale(1.0f);
        aiVector3D translation(0.0f);
        aiQuaternion rotation;
        source.mTransformation.Decompose(scale, rotation, translation);
        const std::uint32_t nodeIndex = static_cast<std::uint32_t>(
            result.model.skeletonNodes.size()
        );
        const std::string name = source.mName.length > 0U
            ? source.mName.C_Str()
            : "Node";
        nodeIndices.emplace(name, nodeIndex);
        result.model.skeletonNodes.push_back(SkeletonNodeData{
            name,
            parentIndex,
            toVec3(translation),
            toQuat(rotation),
            toVec3(scale)
        });
        for (unsigned int childIndex = 0; childIndex < source.mNumChildren; ++childIndex) {
            appendSkeletonNode(*source.mChildren[childIndex], static_cast<std::int32_t>(nodeIndex));
        }
    };
    appendSkeletonNode(*scene->mRootNode, -1);

    ImportContext context{
        *scene,
        result.model,
        result.warnings,
        result.diagnostics,
        nodeIndices,
        {}
    };
    result.model.rootNode = context.appendNode(*scene->mRootNode, aiMatrix4x4{});
    result.model.animations.reserve(scene->mNumAnimations);
    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
        const aiAnimation& sourceAnimation = *scene->mAnimations[animationIndex];
        const double ticksPerSecond = sourceAnimation.mTicksPerSecond > 0.0
            ? sourceAnimation.mTicksPerSecond
            : 25.0;
        AnimationClipData clip;
        clip.name = sourceAnimation.mName.length > 0U
            ? sourceAnimation.mName.C_Str()
            : "Animation " + std::to_string(animationIndex + 1U);
        clip.durationSeconds = static_cast<float>(
            sourceAnimation.mDuration / ticksPerSecond
        );
        clip.channels.reserve(sourceAnimation.mNumChannels);
        for (unsigned int channelIndex = 0; channelIndex < sourceAnimation.mNumChannels; ++channelIndex) {
            const aiNodeAnim& sourceChannel = *sourceAnimation.mChannels[channelIndex];
            const auto node = nodeIndices.find(sourceChannel.mNodeName.C_Str());
            if (node == nodeIndices.end()) continue;
            AnimationChannelData channel;
            channel.nodeIndex = node->second;
            channel.translations.reserve(sourceChannel.mNumPositionKeys);
            for (unsigned int key = 0; key < sourceChannel.mNumPositionKeys; ++key) {
                channel.translations.push_back(AnimationVectorKey{
                    static_cast<float>(sourceChannel.mPositionKeys[key].mTime / ticksPerSecond),
                    toVec3(sourceChannel.mPositionKeys[key].mValue)
                });
            }
            channel.rotations.reserve(sourceChannel.mNumRotationKeys);
            for (unsigned int key = 0; key < sourceChannel.mNumRotationKeys; ++key) {
                channel.rotations.push_back(AnimationRotationKey{
                    static_cast<float>(sourceChannel.mRotationKeys[key].mTime / ticksPerSecond),
                    toQuat(sourceChannel.mRotationKeys[key].mValue)
                });
            }
            channel.scales.reserve(sourceChannel.mNumScalingKeys);
            for (unsigned int key = 0; key < sourceChannel.mNumScalingKeys; ++key) {
                channel.scales.push_back(AnimationVectorKey{
                    static_cast<float>(sourceChannel.mScalingKeys[key].mTime / ticksPerSecond),
                    toVec3(sourceChannel.mScalingKeys[key].mValue)
                });
            }
            clip.channels.push_back(std::move(channel));
        }
        result.model.animations.push_back(std::move(clip));
    }
    if (result.model.meshes.empty()) {
        throw std::runtime_error("Imported model does not contain triangle meshes");
    }
    return result;
}
