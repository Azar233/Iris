#version 330 core
uniform vec4 irisMouse;
uniform vec2 irisViewportResolution;
uniform vec2 irisResolution;
uniform sampler2D irisChannel0;
void mainImage(out vec4 color, in vec2 coord) {
    vec3 inputValue = vec3(irisMouse.xy / irisViewportResolution, irisMouse.z / 7.0) * irisMouse.w;
    color = vec4(inputValue + texture(irisChannel0, coord / irisResolution).rgb * 0.125, 1.0);
}
