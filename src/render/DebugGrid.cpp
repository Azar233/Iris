#include "render/DebugGrid.h"

#include <cstddef>
#include <algorithm>
#include <cmath>
#include <vector>

#include <glad/gl.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "render/Shader.h"

namespace {

struct DebugVertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{1.0f};
};

void appendLine(
    std::vector<DebugVertex>& vertices,
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec4& color
) {
    vertices.push_back(DebugVertex{from, color});
    vertices.push_back(DebugVertex{to, color});
}

void appendPositiveAxis(
    std::vector<DebugVertex>& vertices,
    const glm::vec3& origin,
    const glm::vec3& direction,
    const glm::mat4& viewProjection,
    const glm::mat4& inverseViewProjection,
    const glm::vec2& viewportSize,
    const glm::vec4& color
) {
    constexpr float length = 2.25f;
    const glm::vec3 endpoint = origin + direction * length;
    appendLine(vertices, origin, endpoint, color);
    const glm::vec4 startClip = viewProjection * glm::vec4(origin, 1.0f);
    const glm::vec4 tipClip = viewProjection * glm::vec4(endpoint, 1.0f);
    // Let GL clip the shaft normally, but omit heads behind the camera and
    // view-aligned directions instead of dividing by a vanishing length.
    if (startClip.w <= 0.0001f || tipClip.w <= 0.0001f) return;
    const glm::vec3 startNdc = glm::vec3(startClip) / startClip.w;
    const glm::vec3 tipNdc = glm::vec3(tipClip) / tipClip.w;
    const glm::vec2 delta = glm::vec2(tipNdc - startNdc) * viewportSize * 0.5f;
    const float screenLength = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (screenLength <= 1.0f) return;
    const glm::vec2 forward = delta / screenLength;
    const glm::vec2 side(-forward.y, forward.x);
    const float headLength = std::min(12.0f, screenLength * 0.25f);
    for (float sign : {-1.0f, 1.0f}) {
        const glm::vec2 offset = (-forward * headLength + side * (sign * headLength * 0.5f))
            * 2.0f / viewportSize;
        const glm::vec4 world = inverseViewProjection
            * glm::vec4(tipNdc.x + offset.x, tipNdc.y + offset.y, tipNdc.z, 1.0f);
        appendLine(vertices, endpoint, glm::vec3(world) / world.w, color);
    }
}

} // namespace

DebugGrid::DebugGrid(
    const std::filesystem::path& vertexShaderPath,
    const std::filesystem::path& fragmentShaderPath
) : shader_(std::make_unique<Shader>(vertexShaderPath, fragmentShaderPath)) {
    infiniteShader_ = std::make_unique<Shader>(vertexShaderPath.parent_path() / "fullscreen.vert",
        fragmentShaderPath.parent_path() / "infinite_grid.frag");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(18U * sizeof(DebugVertex)),
        nullptr,
        GL_DYNAMIC_DRAW
    );
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(DebugVertex)),
        reinterpret_cast<void*>(offsetof(DebugVertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(DebugVertex)),
        reinterpret_cast<void*>(offsetof(DebugVertex, color))
    );
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

DebugGrid::~DebugGrid() {
    if (vbo_ != 0U) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0U) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void DebugGrid::draw(
    const glm::mat4& view,
    const glm::mat4& projection,
    bool showGrid,
    bool showAxes
) const {
    if (!showGrid && !showAxes) {
        return;
    }

    glBindVertexArray(vao_);
    if (showGrid) {
        infiniteShader_->use();
        infiniteShader_->setMat4("uViewProjection", projection * view);
        infiniteShader_->setMat4("uInverseViewProjection", glm::inverse(projection * view));
        infiniteShader_->setVec3("uCamera", glm::vec3(glm::inverse(view)[3]));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    shader_->use();
    shader_->setMat4("uView", view);
    shader_->setMat4("uProjection", projection);
    if (showAxes) {
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const glm::vec2 viewportSize(std::max(viewport[2], 1), std::max(viewport[3], 1));
        const glm::mat4 viewProjection = projection * view;
        const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
        std::vector<DebugVertex> vertices;
        vertices.reserve(18);
        appendPositiveAxis(vertices, glm::vec3(0.0f), {1.0f, 0.0f, 0.0f},
            viewProjection, inverseViewProjection, viewportSize, {1.00f, 0.08f, 0.14f, 1.0f});
        appendPositiveAxis(vertices, glm::vec3(0.0f), {0.0f, 1.0f, 0.0f},
            viewProjection, inverseViewProjection, viewportSize, {0.10f, 1.00f, 0.22f, 1.0f});
        appendPositiveAxis(vertices, glm::vec3(0.0f), {0.0f, 0.0f, 1.0f},
            viewProjection, inverseViewProjection, viewportSize, {0.08f, 0.34f, 1.00f, 1.0f});
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(DebugVertex)), vertices.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glLineWidth(2.0f);
        glDrawArrays(
            GL_LINES,
            0,
            static_cast<GLsizei>(vertices.size())
        );
        glLineWidth(1.0f);
    }
    glBindVertexArray(0);
}
