#version 330 core
in vec2 vUv;
out vec4 fragmentColor;
uniform sampler2D uSource;
uniform sampler2D uDepth;
uniform int uMode;
uniform float uNear;
void main() {
    vec4 value = texture(uSource, vUv);
    float depth = texture(uDepth, vUv).r;
    vec3 color = value.rgb;
    if (uMode == 2) color = vec3(value.rg, 0.0);
    // Match the existing contrast-depth debug view, not a metric linear-depth AOV.
    if (uMode == 3) color = vec3(1.0 - pow(clamp(depth, 0.0, 1.0), 3.2 / max(uNear, 0.0001)));
    if (uMode == 4) color = value.z > 0.5 ? vec3(clamp(value.xy * 8.0 + 0.5, 0.0, 1.0), 1.0) : vec3(0.0);
    if (uMode == 5) color = vec3(value.r);
    if (depth >= 0.999999 && uMode != 5) color = vec3(0.0);
    fragmentColor = vec4(color, 1.0);
}
