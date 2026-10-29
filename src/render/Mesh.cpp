#include "render/Mesh.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {

std::vector<std::uint32_t> buildClusteredLod(
    const MeshData& data,
    const std::vector<SubmeshData>& sourceSubmeshes,
    int gridResolution,
    std::vector<SubmeshData>& outputSubmeshes
) {
    std::vector<std::uint32_t> output;
    outputSubmeshes.clear();
    outputSubmeshes.reserve(sourceSubmeshes.size());
    const glm::vec3 extent = glm::max(data.boundsMax - data.boundsMin, glm::vec3(1.0e-5f));

    for (const SubmeshData& source : sourceSubmeshes) {
        SubmeshData lod = source;
        lod.firstIndex = static_cast<std::uint32_t>(output.size());
        std::unordered_map<std::uint64_t, std::uint32_t> representatives;
        const auto representativeFor = [&](std::uint32_t vertexIndex) {
            const glm::vec3 normalized = glm::clamp(
                (data.vertices[vertexIndex].position - data.boundsMin) / extent,
                glm::vec3(0.0f),
                glm::vec3(1.0f)
            );
            const glm::ivec3 cell = glm::ivec3(glm::round(
                normalized * static_cast<float>(gridResolution)
            ));
            const std::uint64_t key = static_cast<std::uint64_t>(cell.x)
                | (static_cast<std::uint64_t>(cell.y) << 12U)
                | (static_cast<std::uint64_t>(cell.z) << 24U);
            const auto [found, inserted] = representatives.emplace(key, vertexIndex);
            static_cast<void>(inserted);
            return found->second;
        };
        const std::size_t begin = source.firstIndex;
        const std::size_t end = std::min(
            begin + static_cast<std::size_t>(source.indexCount),
            data.indices.size()
        );
        for (std::size_t index = begin; index + 2U < end; index += 3U) {
            const std::uint32_t source0 = data.indices[index];
            const std::uint32_t source1 = data.indices[index + 1U];
            const std::uint32_t source2 = data.indices[index + 2U];
            if (source0 >= data.vertices.size()
                || source1 >= data.vertices.size()
                || source2 >= data.vertices.size()) continue;
            const std::uint32_t index0 = representativeFor(source0);
            const std::uint32_t index1 = representativeFor(source1);
            const std::uint32_t index2 = representativeFor(source2);
            if (index0 == index1 || index1 == index2 || index2 == index0) continue;
            output.push_back(index0);
            output.push_back(index1);
            output.push_back(index2);
        }
        if (output.size() == lod.firstIndex) {
            output.insert(
                output.end(),
                data.indices.begin() + static_cast<std::ptrdiff_t>(begin),
                data.indices.begin() + static_cast<std::ptrdiff_t>(end)
            );
        }
        lod.indexCount = static_cast<std::uint32_t>(output.size() - lod.firstIndex);
        outputSubmeshes.push_back(std::move(lod));
    }
    return output;
}

} // namespace

Mesh::Mesh(const MeshData& data)
    : vertexCount_(data.vertices.size()), indexCount_(data.indices.size()), submeshes_(data.submeshes) {
    if (submeshes_.empty() && indexCount_ > 0U) {
        submeshes_.push_back(SubmeshData{
            data.name,
            0U,
            static_cast<std::uint32_t>(indexCount_),
            -1
        });
    }

    submeshCenters_.reserve(submeshes_.size());
    for (const SubmeshData& submesh : submeshes_) {
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        bool hasVertex = false;
        const std::size_t begin = submesh.firstIndex;
        const std::size_t end = std::min(
            begin + static_cast<std::size_t>(submesh.indexCount),
            data.indices.size()
        );
        for (std::size_t index = begin; index < end; ++index) {
            const std::size_t vertexIndex = data.indices[index];
            if (vertexIndex >= data.vertices.size()) {
                continue;
            }
            const glm::vec3& position = data.vertices[vertexIndex].position;
            boundsMin = glm::min(boundsMin, position);
            boundsMax = glm::max(boundsMax, position);
            hasVertex = true;
        }
        submeshBounds_.push_back(hasVertex ? BoundingBox{boundsMin, boundsMax} : BoundingBox{});
        submeshCenters_.push_back(hasVertex ? 0.5f * (boundsMin + boundsMax) : glm::vec3(0.0f));
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(static_cast<GLsizei>(lodEbos_.size()), lodEbos_.data());
    glGenBuffers(1, &instanceVbo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
        data.vertices.data(),
        GL_STATIC_DRAW
    );

    lodSubmeshes_[0] = submeshes_;
    std::array<std::vector<std::uint32_t>, 3> lodIndices;
    lodIndices[0] = data.indices;
    lodIndices[1] = buildClusteredLod(data, submeshes_, 6, lodSubmeshes_[1]);
    lodIndices[2] = buildClusteredLod(data, submeshes_, 3, lodSubmeshes_[2]);
    for (std::size_t lod = 0; lod < lodIndices.size(); ++lod) {
        lodTriangleCounts_[lod] = lodIndices[lod].size() / 3U;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lodEbos_[lod]);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(lodIndices[lod].size() * sizeof(std::uint32_t)),
            lodIndices[lod].data(),
            GL_STATIC_DRAW
        );
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lodEbos_[0]);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, normal))
    );
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, texCoord0))
    );
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, tangent))
    );
    glEnableVertexAttribArray(8);
    glVertexAttribIPointer(
        8,
        4,
        GL_UNSIGNED_INT,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, jointIndices))
    );
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(
        9,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(Vertex)),
        reinterpret_cast<void*>(offsetof(Vertex, jointWeights))
    );

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    const glm::mat4 identityInstance(1.0f);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(sizeof(identityInstance)),
        &identityInstance,
        GL_STREAM_DRAW
    );
    for (unsigned int column = 0U; column < 4U; ++column) {
        const unsigned int location = 4U + column;
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(glm::mat4)),
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(column * sizeof(glm::vec4)))
        );
        glVertexAttribDivisor(location, 1U);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Mesh::~Mesh() {
    glDeleteBuffers(static_cast<GLsizei>(lodEbos_.size()), lodEbos_.data());
    if (instanceVbo_ != 0U) glDeleteBuffers(1, &instanceVbo_);
    if (vbo_ != 0U) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0U) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void Mesh::draw(const MaterialBinder& bindMaterial) const {
    glBindVertexArray(vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lodEbos_[0]);
    for (const auto& submesh : submeshes_) {
        bindMaterial(submesh.materialIndex);
        const auto byteOffset = static_cast<std::uintptr_t>(submesh.firstIndex) * sizeof(std::uint32_t);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(submesh.indexCount),
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(byteOffset)
        );
    }
    glBindVertexArray(0);
}

void Mesh::drawSubmesh(std::size_t index) const {
    if (index >= submeshes_.size()) {
        throw std::out_of_range("Mesh submesh index is out of range");
    }
    const SubmeshData& submesh = submeshes_[index];
    const auto byteOffset = static_cast<std::uintptr_t>(submesh.firstIndex) * sizeof(std::uint32_t);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lodEbos_[0]);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(submesh.indexCount),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(byteOffset)
    );
    glBindVertexArray(0);
}

void Mesh::drawSubmeshInstanced(
    std::size_t index,
    std::size_t lodLevel,
    const std::vector<glm::mat4>& modelMatrices
) const {
    if (modelMatrices.empty()) return;
    lodLevel = std::min(lodLevel, lodSubmeshes_.size() - 1U);
    if (index >= lodSubmeshes_[lodLevel].size()) {
        throw std::out_of_range("Mesh LOD submesh index is out of range");
    }
    const SubmeshData& submesh = lodSubmeshes_[lodLevel][index];
    const auto byteOffset = static_cast<std::uintptr_t>(submesh.firstIndex)
        * sizeof(std::uint32_t);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(modelMatrices.size() * sizeof(glm::mat4)),
        modelMatrices.data(),
        GL_STREAM_DRAW
    );
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lodEbos_[lodLevel]);
    glDrawElementsInstanced(
        GL_TRIANGLES,
        static_cast<GLsizei>(submesh.indexCount),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(byteOffset),
        static_cast<GLsizei>(modelMatrices.size())
    );
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

std::size_t Mesh::lodTriangleCount(std::size_t lodLevel) const {
    return lodTriangleCounts_[std::min(lodLevel, lodTriangleCounts_.size() - 1U)];
}

std::int32_t Mesh::submeshMaterialIndex(std::size_t index) const {
    if (index >= submeshes_.size()) {
        throw std::out_of_range("Mesh submesh index is out of range");
    }
    return submeshes_[index].materialIndex;
}

const glm::vec3& Mesh::submeshCenter(std::size_t index) const {
    if (index >= submeshCenters_.size()) {
        throw std::out_of_range("Mesh submesh index is out of range");
    }
    return submeshCenters_[index];
}
