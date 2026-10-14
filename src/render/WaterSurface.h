#pragma once

#include <cstddef>

// The logical grid is warped about the camera in water.vert. Near cells are fine and far cells
// grow continuously, avoiding clipmap seams while the fixed index buffer is reused every frame.
class WaterSurface {
public:
    explicit WaterSurface(int resolution);
    ~WaterSurface();
    WaterSurface(const WaterSurface&) = delete;
    WaterSurface& operator=(const WaterSurface&) = delete;

    unsigned int vertexArray() const { return vertexArray_; }
    std::size_t indexCount() const { return indexCount_; }
    std::size_t triangleCount() const { return indexCount_ / 3U; }
    std::size_t estimatedBytes() const { return bytes_; }

private:
    unsigned int vertexArray_{0};
    unsigned int vertexBuffer_{0};
    unsigned int indexBuffer_{0};
    std::size_t indexCount_{0};
    std::size_t bytes_{0};
};
