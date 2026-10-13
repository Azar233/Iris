#version 330 core
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord0;
uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;
uniform float gain;
uniform float frequency;
uniform float irisTime;
out vec4 FragColor;
void main() {
    vec3 base = uBaseColor.rgb;
    base *= texture(uBaseColorTexture, vTexCoord0).rgb;
    vec3 normal = abs(normalize(vWorldNormal));
    vec2 uv = normal.z >= max(normal.x, normal.y) ? vWorldPosition.xy
        : normal.x >= normal.y ? vWorldPosition.yz : vWorldPosition.xz;
    float checker = mod(dot(floor(uv * frequency), vec2(1.0)), 2.0);
    vec3 tint = mix(vec3(0.04, 0.18, 0.50), vec3(0.95, 0.42, 0.08), checker);
    float diffuse = 0.25 + 0.75 * max(dot(normalize(vWorldNormal), normalize(vec3(1, 2, 3))), 0.0);
    float pulse = 0.85 + 0.15 * cos(irisTime);
    FragColor = vec4(base * tint * diffuse * gain * pulse, 1.0);
}
