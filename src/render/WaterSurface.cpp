#include "render/WaterSurface.h"

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include "render/WaterWaves.h"

WaterSurface::WaterSurface(int resolution) {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>(resolution + 1) * (resolution + 1) * 2U);
    indices.reserve(static_cast<std::size_t>(resolution) * resolution * 6U);
    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            vertices.push_back(2.0f * static_cast<float>(x) / resolution - 1.0f);
            vertices.push_back(2.0f * static_cast<float>(z) / resolution - 1.0f);
        }
    }
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const auto first = static_cast<std::uint32_t>(z * (resolution + 1) + x);
            const auto next = first + static_cast<std::uint32_t>(resolution + 1);
            indices.insert(indices.end(), {first, next, first + 1U, first + 1U, next, next + 1U});
        }
    }
    indexCount_ = indices.size();
    bytes_ = vertices.size() * sizeof(float) + indices.size() * sizeof(std::uint32_t);
    GLint previousVao = 0, previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGenVertexArrays(1, &vertexArray_);
    glGenBuffers(1, &vertexBuffer_);
    glGenBuffers(1, &indexBuffer_);
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(previousVao);
    glBindBuffer(GL_ARRAY_BUFFER, previousArrayBuffer);
}

WaterSurface::~WaterSurface() {
    if (indexBuffer_ != 0U) glDeleteBuffers(1, &indexBuffer_);
    if (vertexBuffer_ != 0U) glDeleteBuffers(1, &vertexBuffer_);
    if (vertexArray_ != 0U) glDeleteVertexArrays(1, &vertexArray_);
}
