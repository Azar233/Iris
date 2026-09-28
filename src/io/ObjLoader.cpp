#include "asset/InputManifest.h"
#include "io/ObjLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/geometric.hpp>
#include <tiny_obj_loader.h>

namespace {
class CaptureMaterialReader final : public tinyobj::MaterialReader {
public:
    explicit CaptureMaterialReader(const std::filesystem::path& root):root_(root),reader_(root.string()+"/"){}
    bool operator()(const std::string& name,std::vector<tinyobj::material_t>* materials,
        std::map<std::string,int>* map,std::string* error) override {
        capture::recordInput(root_/name);
        return reader_(name,materials,map,error);
    }
private:
    std::filesystem::path root_;
    tinyobj::MaterialFileReader reader_;
};


struct VertexKey {
    int position{-1};
    int normal{-1};
    int texCoord{-1};
    std::uint64_t split{0};

    bool operator==(const VertexKey& other) const {
        return position == other.position
            && normal == other.normal
            && texCoord == other.texCoord
            && split == other.split;
    }
};

void hashCombine(std::size_t& result, std::size_t value) {
    result ^= value + 0x9e3779b9U + (result << 6U) + (result >> 2U);
}

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& key) const {
        std::size_t result = static_cast<std::size_t>(static_cast<std::uint32_t>(key.position));
        hashCombine(result, static_cast<std::size_t>(static_cast<std::uint32_t>(key.normal)));
        hashCombine(result, static_cast<std::size_t>(static_cast<std::uint32_t>(key.texCoord)));
        hashCombine(result, static_cast<std::size_t>(key.split));
        return result;
    }
};

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

glm::vec3 readVec3(const std::vector<tinyobj::real_t>& values, int index, const char* label) {
    const std::size_t base = static_cast<std::size_t>(index) * 3U;
    if (index < 0 || base + 2U >= values.size()) {
        throw std::runtime_error(std::string("OBJ contains an invalid ") + label + " index");
    }
    return {
        static_cast<float>(values[base]),
        static_cast<float>(values[base + 1U]),
        static_cast<float>(values[base + 2U])
    };
}

glm::vec2 readVec2(const std::vector<tinyobj::real_t>& values, int index, const char* label) {
    const std::size_t base = static_cast<std::size_t>(index) * 2U;
    if (index < 0 || base + 1U >= values.size()) {
        throw std::runtime_error(std::string("OBJ contains an invalid ") + label + " index");
    }
    return {
        static_cast<float>(values[base]),
        static_cast<float>(values[base + 1U])
    };
}

bool requestsFlatShading(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] != 's') {
            continue;
        }
        const auto value = line.find_first_not_of(" \t", first + 1U);
        if (value != std::string::npos
            && (line.compare(value, 3U, "off") == 0 || line[value] == '0')) {
            return true;
        }
    }
    return false;
}

std::filesystem::path resolveTexturePath(
    const std::filesystem::path& baseDirectory,
    const std::string& textureName
) {
    return textureName.empty()
        ? std::filesystem::path{}
        : (baseDirectory / std::filesystem::path(textureName)).lexically_normal();
}

void generateTangents(MeshData& mesh, ModelImportResult& result) {
    std::vector<glm::vec3> tangentAccumulation(mesh.vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangentAccumulation(mesh.vertices.size(), glm::vec3(0.0f));
    std::size_t degenerateUvTriangles = 0;

    for (std::size_t i = 0; i + 2U < mesh.indices.size(); i += 3U) {
        const std::uint32_t index0 = mesh.indices[i];
        const std::uint32_t index1 = mesh.indices[i + 1U];
        const std::uint32_t index2 = mesh.indices[i + 2U];
        const Vertex& v0 = mesh.vertices[index0];
        const Vertex& v1 = mesh.vertices[index1];
        const Vertex& v2 = mesh.vertices[index2];
        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;
        const glm::vec2 uv1 = v1.texCoord0 - v0.texCoord0;
        const glm::vec2 uv2 = v2.texCoord0 - v0.texCoord0;
        const float determinant = uv1.x * uv2.y - uv1.y * uv2.x;
        if (std::abs(determinant) <= 1e-10f) {
            ++degenerateUvTriangles;
            continue;
        }
        const float reciprocal = 1.0f / determinant;
        const glm::vec3 tangent = (edge1 * uv2.y - edge2 * uv1.y) * reciprocal;
        const glm::vec3 bitangent = (edge2 * uv1.x - edge1 * uv2.x) * reciprocal;
        for (const std::uint32_t index : {index0, index1, index2}) {
            tangentAccumulation[index] += tangent;
            bitangentAccumulation[index] += bitangent;
        }
    }

    std::size_t invalidVertices = 0;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vertex& vertex = mesh.vertices[i];
        const glm::vec3 normal = glm::normalize(vertex.normal);
        glm::vec3 tangent = tangentAccumulation[i] - normal * glm::dot(normal, tangentAccumulation[i]);
        if (glm::dot(tangent, tangent) <= 1e-12f) {
            const glm::vec3 axis = std::abs(normal.y) < 0.999f
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            tangent = glm::normalize(glm::cross(axis, normal));
            vertex.tangent = glm::vec4(tangent, 0.0f);
            ++invalidVertices;
            continue;
        }
        tangent = glm::normalize(tangent);
        const float handedness = glm::dot(glm::cross(normal, tangent), bitangentAccumulation[i]) < 0.0f
            ? -1.0f
            : 1.0f;
        vertex.tangent = glm::vec4(tangent, handedness);
    }

    if (degenerateUvTriangles > 0U || invalidVertices > 0U) {
        result.addDiagnostic(
            ModelDiagnosticScope::Mesh,
            ModelDiagnosticSeverity::Warning,
            mesh.name,
            "Tangent generation skipped " + std::to_string(degenerateUvTriangles)
                + " triangle(s) with degenerate UVs; " + std::to_string(invalidVertices)
                + " vertex tangent(s) will use geometric normals."
        );
    }
}

} // namespace

bool ObjLoader::supports(const std::filesystem::path& path) const {
    return lowercase(path.extension().string()) == ".obj";
}

ModelImportResult ObjLoader::load(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Model file does not exist: " + path.string());
    }

    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string diagnostics;
    const std::filesystem::path baseDirectory = path.parent_path();
    capture::recordInput(path);
    std::ifstream objStream(path);
    CaptureMaterialReader materialReader(baseDirectory);
    const bool loaded = tinyobj::LoadObj(&attributes,&shapes,&materials,&diagnostics,
        &objStream,&materialReader,true);
    if (!loaded) {
        throw std::runtime_error("Failed to parse OBJ: " + diagnostics);
    }
    if (shapes.empty() || attributes.vertices.empty()) {
        throw std::runtime_error("OBJ does not contain renderable mesh data");
    }

    ModelImportResult result;
    result.model.name = path.stem().string();
    result.model.sourcePath = std::filesystem::absolute(path).lexically_normal();
    result.model.rootNode.name = result.model.name;
    if (!diagnostics.empty()) {
        result.addDiagnostic(
            ModelDiagnosticScope::File,
            ModelDiagnosticSeverity::Warning,
            result.model.sourcePath.filename().string(),
            diagnostics
        );
    }

    std::unordered_map<std::string, std::int32_t> textureIndices;
    auto appendTexture = [&](const std::string& textureName, bool srgb) -> std::int32_t {
        const std::filesystem::path texturePath = resolveTexturePath(baseDirectory, textureName);
        if (texturePath.empty()) {
            return -1;
        }
        const std::filesystem::path absolutePath = std::filesystem::absolute(texturePath).lexically_normal();
        const std::string cacheKey = absolutePath.generic_string() + (srgb ? "::srgb" : "::linear");
        const auto found = textureIndices.find(cacheKey);
        if (found != textureIndices.end()) {
            return found->second;
        }

        TextureData texture;
        texture.name = absolutePath.filename().string();
        texture.cacheKey = cacheKey;
        texture.sourcePath = absolutePath;
        texture.srgb = srgb;
        const auto index = static_cast<std::int32_t>(result.model.textures.size());
        result.model.textures.push_back(std::move(texture));
        textureIndices.emplace(cacheKey, index);
        return index;
    };

    for (const auto& sourceMaterial : materials) {
        MaterialData material;
        material.name = sourceMaterial.name.empty() ? "Material" : sourceMaterial.name;
        material.emissiveFactor = glm::vec3(sourceMaterial.emission[0], sourceMaterial.emission[1], sourceMaterial.emission[2]);
        material.baseColorFactor = glm::vec4(
            static_cast<float>(sourceMaterial.diffuse[0]),
            static_cast<float>(sourceMaterial.diffuse[1]),
            static_cast<float>(sourceMaterial.diffuse[2]),
            static_cast<float>(sourceMaterial.dissolve)
        );
        if (sourceMaterial.dissolve < 0.999) {
            material.alphaMode = MaterialAlphaMode::Blend;
        }
        material.baseColorTextureIndex = appendTexture(sourceMaterial.diffuse_texname, true);
        material.normalTextureIndex = appendTexture(
            sourceMaterial.normal_texname,
            false
        );
        material.metallicFactor = std::clamp(static_cast<float>(sourceMaterial.metallic), 0.0f, 1.0f);
        // tinyobj uses zero for an absent Pr extension; keep the renderer's
        // conservative rough default unless the MTL explicitly supplied a value.
        if (sourceMaterial.roughness > 0.0) {
            material.roughnessFactor = std::clamp(static_cast<float>(sourceMaterial.roughness), 0.04f, 1.0f);
        }
        if (sourceMaterial.ior >= 1.0) {
            material.indexOfRefraction = static_cast<float>(sourceMaterial.ior);
        }
        result.model.materials.push_back(std::move(material));
    }

    MeshData output;
    output.name = result.model.name;
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> uniqueVertices;
    bool normalsComplete = true;
    bool hasTextureCoordinates = false;
    const bool flatShading = requestsFlatShading(path);
    std::uint64_t cornerSequence = 0;
    std::unordered_set<int> reportedInvalidMaterials;

    auto appendVertex = [&](const tinyobj::index_t& index) -> std::uint32_t {
        const std::uint64_t split = flatShading && index.normal_index < 0 ? ++cornerSequence : 0U;
        const VertexKey key{index.vertex_index, index.normal_index, index.texcoord_index, split};
        const auto found = uniqueVertices.find(key);
        if (found != uniqueVertices.end()) {
            return found->second;
        }

        Vertex vertex;
        vertex.position = readVec3(attributes.vertices, index.vertex_index, "position");
        if (index.normal_index >= 0) {
            vertex.normal = readVec3(attributes.normals, index.normal_index, "normal");
            const float lengthSquared = glm::dot(vertex.normal, vertex.normal);
            if (lengthSquared > 1e-12f) {
                vertex.normal = glm::normalize(vertex.normal);
            } else {
                normalsComplete = false;
                vertex.normal = glm::vec3(0.0f);
            }
        } else {
            normalsComplete = false;
            vertex.normal = glm::vec3(0.0f);
        }
        if (index.texcoord_index >= 0) {
            vertex.texCoord0 = readVec2(attributes.texcoords, index.texcoord_index, "texture coordinate");
            hasTextureCoordinates = true;
        }

        output.boundsMin = glm::min(output.boundsMin, vertex.position);
        output.boundsMax = glm::max(output.boundsMax, vertex.position);
        const auto newIndex = static_cast<std::uint32_t>(output.vertices.size());
        output.vertices.push_back(vertex);
        uniqueVertices.emplace(key, newIndex);
        return newIndex;
    };

    for (std::size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex) {
        const auto& shape = shapes[shapeIndex];
        const std::string shapeName = shape.name.empty()
            ? "Shape " + std::to_string(shapeIndex)
            : shape.name;
        std::size_t offset = 0;
        std::size_t firstIndex = output.indices.size();
        int currentMaterial = -1;
        bool hasActiveSubmesh = false;
        std::size_t submeshSequence = 0;

        auto closeSubmesh = [&]() {
            const std::size_t indexCount = output.indices.size() - firstIndex;
            if (!hasActiveSubmesh || indexCount == 0U) {
                return;
            }
            output.submeshes.push_back(SubmeshData{
                shapeName + " " + std::to_string(submeshSequence++),
                static_cast<std::uint32_t>(firstIndex),
                static_cast<std::uint32_t>(indexCount),
                currentMaterial
            });
        };

        for (std::size_t faceIndex = 0; faceIndex < shape.mesh.num_face_vertices.size(); ++faceIndex) {
            const unsigned char faceVertexCount = shape.mesh.num_face_vertices[faceIndex];
            if (faceVertexCount < 3U) {
                offset += faceVertexCount;
                continue;
            }

            int faceMaterial = faceIndex < shape.mesh.material_ids.size()
                ? shape.mesh.material_ids[faceIndex]
                : -1;
            if (faceMaterial >= 0 && static_cast<std::size_t>(faceMaterial) >= materials.size()) {
                if (reportedInvalidMaterials.insert(faceMaterial).second) {
                    result.addDiagnostic(
                        ModelDiagnosticScope::Material,
                        ModelDiagnosticSeverity::Warning,
                        "material #" + std::to_string(faceMaterial),
                        "Shape " + shapeName + " references a missing material; using the default material."
                    );
                }
                faceMaterial = -1;
            }
            if (!hasActiveSubmesh) {
                currentMaterial = faceMaterial;
                firstIndex = output.indices.size();
                hasActiveSubmesh = true;
            } else if (faceMaterial != currentMaterial) {
                closeSubmesh();
                currentMaterial = faceMaterial;
                firstIndex = output.indices.size();
            }

            std::vector<std::uint32_t> face;
            face.reserve(faceVertexCount);
            for (unsigned char vertex = 0; vertex < faceVertexCount; ++vertex) {
                if (offset + vertex >= shape.mesh.indices.size()) {
                    throw std::runtime_error("OBJ face index data is truncated");
                }
                face.push_back(appendVertex(shape.mesh.indices[offset + vertex]));
            }
            for (std::size_t vertex = 1; vertex + 1 < face.size(); ++vertex) {
                output.indices.push_back(face[0]);
                output.indices.push_back(face[vertex]);
                output.indices.push_back(face[vertex + 1]);
            }
            offset += faceVertexCount;
        }
        closeSubmesh();
    }

    if (output.indices.empty()) {
        throw std::runtime_error("OBJ does not contain any triangle faces");
    }

    if (!normalsComplete) {
        for (auto& vertex : output.vertices) {
            vertex.normal = glm::vec3(0.0f);
        }
        std::size_t degenerateTriangles = 0;
        for (std::size_t i = 0; i + 2 < output.indices.size(); i += 3) {
            Vertex& a = output.vertices[output.indices[i]];
            Vertex& b = output.vertices[output.indices[i + 1]];
            Vertex& c = output.vertices[output.indices[i + 2]];
            const glm::vec3 weightedNormal = glm::cross(b.position - a.position, c.position - a.position);
            if (glm::dot(weightedNormal, weightedNormal) <= 1e-16f) {
                ++degenerateTriangles;
                continue;
            }
            a.normal += weightedNormal;
            b.normal += weightedNormal;
            c.normal += weightedNormal;
        }
        for (auto& vertex : output.vertices) {
            if (glm::dot(vertex.normal, vertex.normal) > 1e-16f) {
                vertex.normal = glm::normalize(vertex.normal);
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
        if (degenerateTriangles > 0) {
            result.addDiagnostic(
                ModelDiagnosticScope::Mesh,
                ModelDiagnosticSeverity::Warning,
                output.name,
                "Skipped " + std::to_string(degenerateTriangles)
                    + " degenerate triangle(s) while generating normals."
            );
        }
    }

    if (hasTextureCoordinates) {
        generateTangents(output, result);
    }

    result.model.boundsMin = output.boundsMin;
    result.model.boundsMax = output.boundsMax;
    result.model.meshes.push_back(std::move(output));
    result.model.rootNode.meshIndices.push_back(0U);
    return result;
}
