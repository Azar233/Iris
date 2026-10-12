#version 330 core
uniform sampler2D irisChannel0;
uniform sampler2D irisChannel1;
uniform vec2 irisResolution;
uniform float irisTime;
uniform bool irisHistoryValid;
out vec4 color;
void main() {
    vec2 uv = gl_FragCoord.xy / irisResolution;
    float feedback = texture(irisChannel0, uv).r;
    float noise = texture(irisChannel1, uv * 4.0).r;
    color = vec4(feedback, noise * 0.5 + (irisHistoryValid ? 0.25 : 0.0), irisTime, 1);
}
