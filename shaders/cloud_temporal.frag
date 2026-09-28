#version 330 core
in vec2 vUv;
uniform sampler2D uCurrent;
uniform sampler2D uVolumeDepth;
uniform sampler2D uHistory;
uniform sampler2D uHistoryDepth;
uniform bool uHistoryValid;
uniform vec3 uCameraPosition, uPreviousPosition;
uniform vec3 uCameraForward, uCameraRight, uCameraUp;
uniform vec2 uHalfExtent, uWindDelta;
uniform mat4 uPreviousViewProjection;
layout(location = 0) out vec4 outCloud;
layout(location = 1) out vec2 outDepth;

void main() {
    ivec2 size = textureSize(uCurrent, 0);
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
    vec4 current = texelFetch(uCurrent, pixel, 0);
    vec2 depths = texelFetch(uVolumeDepth, pixel, 0).rg;
    outCloud = current;
    outDepth = depths;
    if (!uHistoryValid || depths.x <= 0.0) return;

    vec3 ray = normalize(uCameraForward + uCameraRight * (vUv.x * 2.0 - 1.0) * uHalfExtent.x
        + uCameraUp * (vUv.y * 2.0 - 1.0) * uHalfExtent.y);
    // Density is evaluated at world position + wind offset. Hence the same cloud feature in the
    // previous frame is at current position + currentWind - previousWind (not the opposite sign).
    vec3 translation = vec3(uWindDelta.x, 0.0, uWindDelta.y);
    vec3 entry = uCameraPosition + ray * max(depths.y, 1.0) + translation;
    vec4 previousClip = uPreviousViewProjection * vec4(entry, 1.0);
    if (previousClip.w <= 0.0) return;
    vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    vec2 margin = 0.5 / vec2(size);
    if (any(lessThan(previousUv, margin)) || any(greaterThan(previousUv, 1.0 - margin))) return;
    float previousDepth = texture(uHistoryDepth, previousUv).r;
    float expectedDepth = length(uCameraPosition + ray * depths.x + translation - uPreviousPosition);
    if (previousDepth <= 0.0 || abs(previousDepth - expectedDepth) > max(50.0, expectedDepth * 0.12)) return;

    vec4 lo = current, hi = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec4 neighbour = texelFetch(uCurrent, clamp(pixel + ivec2(x,y), ivec2(0), size - 1), 0);
            lo = min(lo, neighbour);
            hi = max(hi, neighbour);
        }
    }
    vec4 history = clamp(texture(uHistory, previousUv), lo, hi);
    outCloud = mix(current, history, 0.85);
}
