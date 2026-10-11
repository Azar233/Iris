#version 330 core
out vec4 color;
uniform sampler2D irisChannel0;
uniform vec2 irisResolution;
uniform float irisTime;
uniform float decay;
void main() {
    vec2 uv = gl_FragCoord.xy / irisResolution;
    vec2 center = vec2(0.5) + vec2(cos(irisTime * 2.3), sin(irisTime * 3.1)) * vec2(0.30, 0.28);
    vec2 p = (uv - center) * vec2(irisResolution.x / irisResolution.y, 1.0);
    float dotGlow = exp(-dot(p, p) * 1200.0);
    vec3 fresh = dotGlow * vec3(1.8, 0.55, 0.15);
    color = vec4(texture(irisChannel0, uv).rgb * decay + fresh, 1.0);
}
