#version 330 core
uniform vec2 irisResolution;
uniform float irisTime;
uniform float noiseScale;
uniform sampler2D irisChannel0;
uniform sampler3D irisChannel1;
uniform sampler3D irisChannel2;
uniform sampler2D irisChannel3;
void mainImage(out vec4 color, in vec2 coord) {
    vec2 uv = coord / irisResolution;
    vec2 panel = vec2(fract(uv.x * 3.0), uv.y);
    float slice = 0.2 + 0.6 * fract(irisTime * 0.1);
    vec3 linear;
    if (uv.x < 1.0 / 3.0) linear = texture(irisChannel0, panel).rgb;
    else if (uv.x < 2.0 / 3.0) {
        float density = texture(irisChannel1, vec3(panel, slice)).r;
        linear = mix(vec3(0.025, 0.08, 0.16), vec3(0.1, 0.8, 0.45), density);
    } else {
        float noise = texture(irisChannel2, vec3(panel * noiseScale, slice)).r;
        float grain = texture(irisChannel3, panel).r;
        linear = mix(vec3(0.04, 0.05, 0.12), vec3(0.7, 0.3, 0.1), noise * 0.8 + grain * 0.2);
    }
    if (panel.x < 0.02 || panel.y < 0.02) linear = vec3(0.01);
    color = vec4(pow(clamp(linear, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
