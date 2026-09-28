#version 330 core
#include "cloud_shadow_sample.glsl"
in vec2 vUv;
uniform sampler2D uCloud, uDepth;
uniform vec2 uSunUv;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform float uRange, uStrength;
uniform int uSteps;
layout(location=0) out float outScattering;
void main() {
    // A sky-only approximation. Never add a half-resolution shaft over foreground surfaces.
    if (texture(uDepth, vUv).r < 0.999999) { outScattering = 0.0; return; }
    vec2 delta = (uSunUv - vUv) / float(uSteps);
    float sum = 0.0, weights = 0.0;
    for (int i = 0; i < uSteps; ++i) {
        vec2 uv = vUv + delta * (float(i) + 0.5);
        float weight = exp(-2.0 * (float(i) + 0.5) / float(uSteps));
        // Geometry and cloud extinction both occlude the radial light-source mask.
        float visible = texture(uDepth, uv).r >= 0.999999 ? texture(uCloud, uv).a : 0.0;
        vec4 farPoint = uInverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
        vec3 direction = normalize(farPoint.xyz / farPoint.w - uCameraPosition);
        vec3 scatteringPoint = uCameraPosition + direction * uRange;
        visible *= cloudShadowTransmittance(scatteringPoint);
        sum += visible * weight;
        weights += weight;
    }
    outScattering = uStrength * sum / max(weights, 0.00001)
        * exp(-4.0 * dot(vUv - uSunUv, vUv - uSunUv));
}
