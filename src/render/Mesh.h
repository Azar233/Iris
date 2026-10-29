#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "asset/ModelData.h"
#include "render/SceneDrawList.h"

class Mesh {
public:
    explicit Mesh(const MeshData& data);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    using MaterialBinder = std::function<void(std::int32_t)>;

    void draw(const MaterialBinder& bindMaterial) const;
    void drawSubmesh(std::size_t index) const;
    void drawSubmeshInstanced(
        std::size_t index,
        std::size_t lodLevel,
        const std::vector<glm::mat4>& modelMatrices
    ) const;
    std::int32_t submeshMaterialIndex(std::size_t index) const;
    const glm::vec3& submeshCenter(std::size_t index) const;
    const BoundingBox& submeshBounds(std::size_t index) const { return submeshBounds_.at(index); }
    std::size_t submeshTriangleCount(std::size_t index) const { return submeshes_.at(index).indexCount / 3U; }
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t triangleCount() const { return indexCount_ / 3; }
    std::size_t lodTriangleCount(std::size_t lodLevel) const;
    std::size_t submeshCount() const { return submeshes_.size(); }

private:
    unsigned int vao_{0};
    unsigned int vbo_{0};
    unsigned int instanceVbo_{0};
    std::array<unsigned int, 3> lodEbos_{};
    std::size_t vertexCount_{0};
    std::size_t indexCount_{0};
    std::vector<SubmeshData> submeshes_;
    std::array<std::vector<SubmeshData>, 3> lodSubmeshes_;
    std::array<std::size_t, 3> lodTriangleCounts_{};
    std::vector<glm::vec3> submeshCenters_;
    std::vector<BoundingBox> submeshBounds_;
};
