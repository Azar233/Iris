#version 330 core
out vec4 color;
uniform sampler2D irisChannel0;
uniform vec2 irisResolution;
uniform vec2 irisChannelResolution0;
void main() {
    vec2 uv = gl_FragCoord.xy / irisResolution;
    vec2 texel = 1.0 / irisChannelResolution0;
    vec3 sum = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) sum += texture(irisChannel0, uv + vec2(x, y) * texel).rgb;
    color = vec4(sum / 9.0, 1.0);
}
