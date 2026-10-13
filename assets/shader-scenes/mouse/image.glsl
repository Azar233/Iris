#version 330 core
uniform vec4 irisMouse;
uniform vec2 irisViewportResolution;
uniform vec2 irisResolution;
uniform sampler2D irisChannel0;
void mainImage(out vec4 color, in vec2 coord) {
    vec2 uv = coord / irisResolution;
    vec2 delta = (uv - irisMouse.xy / irisViewportResolution) * irisViewportResolution;
    float cursor = (1.0 - smoothstep(14.0, 16.0, length(delta))) * irisMouse.w;
    vec3 inputValue = texture(irisChannel0, uv).rgb;
    color = vec4(mix(vec3(0.06, 0.08, 0.12) + inputValue * 0.35, vec3(1.0, 0.7, 0.2), cursor), 1.0);
}
