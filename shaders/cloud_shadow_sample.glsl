uniform bool uCloudShadowEnabled;
uniform sampler2D uCloudShadowMap;
uniform vec3 uCloudShadowAxisX, uCloudShadowAxisY;
uniform vec2 uCloudShadowCenter;
uniform float uCloudShadowExtent, uCloudShadowBase;
float cloudShadowTransmittance(vec3 worldPosition) {
    if (!uCloudShadowEnabled || worldPosition.y >= uCloudShadowBase) return 1.0;
    vec2 coordinate = vec2(dot(worldPosition, uCloudShadowAxisX), dot(worldPosition, uCloudShadowAxisY));
    vec2 uv = (coordinate - uCloudShadowCenter) / (2.0 * uCloudShadowExtent) + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    // Fade the finite map boundary; never extend a dark edge texel across the world.
    float margin = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    return mix(1.0, texture(uCloudShadowMap, uv).r, smoothstep(0.0, 0.03, margin));
}
