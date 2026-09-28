#version 330 core
#define MYRENDERER_CLOUD_INLINE
#include "cloud_noise_sample.glsl"
#include "src/optics/CloudField.h"
in vec2 vUv;
uniform MyRendererCloudParams uLayer;
uniform vec3 uAxisX, uAxisY, uSunDirection;
uniform vec2 uCenter;
uniform float uExtent, uExtinction;
uniform int uSteps;
layout(location = 0) out float outTransmission;
void main() {
    vec2 coordinate = uCenter + (vUv * 2.0 - 1.0) * uExtent;
    vec3 origin = uAxisX * coordinate.x + uAxisY * coordinate.y;
    // Integrate the full slab along this sun ray, even if the orthographic plane lies inside or
    // above the slab. The map is consumed only by receivers below cloud base.
    float start = (uLayer.baseHeight - origin.y) / uSunDirection.y;
    float step = (uLayer.topHeight - uLayer.baseHeight) / uSunDirection.y / float(uSteps);
    float opticalDepth = 0.0;
    for (int i = 0; i < uSteps; ++i) {
        vec3 position = origin + uSunDirection * (start + (float(i) + 0.5) * step);
        opticalDepth += myrenderer_cloud_density(position.x, position.y, position.z, uLayer) * step;
    }
    outTransmission = cloudTransmission(opticalDepth * uExtinction, uLayer.offlineNoise);
}
