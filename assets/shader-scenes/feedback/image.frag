#version 330 core
out vec4 color;
uniform sampler2D irisChannel0;
uniform vec2 irisResolution;
void main() {
    vec3 linear = texture(irisChannel0, gl_FragCoord.xy / irisResolution).rgb + vec3(0.012, 0.022, 0.045);
    color = vec4(pow(linear / (vec3(1.0) + linear), vec3(1.0 / 2.2)), 1.0);
}
