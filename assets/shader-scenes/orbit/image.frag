#version 330 core
out vec4 fragColor;
uniform float irisTime;
uniform vec2 irisResolution;
uniform vec3 irisCameraPosition;
uniform float ringWidth;
void main() {
    vec2 p = (2.0 * gl_FragCoord.xy - irisResolution) / irisResolution.y;
    float angle = irisTime * 0.35 + irisCameraPosition.x * 0.04;
    vec2 orbit = vec2(cos(angle), sin(angle)) * 0.55;
    float aa = 2.0 / irisResolution.y;
    float ring = 1.0 - smoothstep(ringWidth, ringWidth + aa, abs(length(p) - 0.55));
    float moon = 1.0 - smoothstep(0.085, 0.085 + aa, length(p - orbit));
    vec3 linear = vec3(0.015, 0.028, 0.065) + ring * vec3(0.10, 0.35, 0.55)
        + moon * vec3(0.80, 0.40, 0.12);
    // v1 writes display encoded RGBA8; there is no second host tonemap.
    fragColor = vec4(pow(clamp(linear, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
