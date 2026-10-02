#version 330 core

layout (location = 0) in vec2 aLogicalPosition;

uniform mat4 uCurrentViewProjection;
uniform mat4 uPreviousViewProjection;
uniform vec3 uCameraPosition;
uniform float uExtent;
uniform float uGridResolution;
uniform float uNearMeshFocus;
uniform float uLevel;
uniform float uTime;
uniform float uPreviousTime;
uniform float uSpeed;
uniform float uSteepness;
uniform float uWaveDiversity;
uniform vec4 uWaves[8]; // direction.xy, amplitude, wavelength
uniform int uWaveCount;
uniform bool uMotionHistoryValid;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec3 vVelocity;
out float vFoam;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
out float vMotionValid;

const float PI = 3.14159265358979323846;

float gridCoordinate(float coordinate) {
    float radius = abs(coordinate);
    float quadratic = radius * radius * uExtent;
    if (uNearMeshFocus <= 0.0) return sign(coordinate) * quadratic;
    float focused = 0.6 * (exp(log(1.0 + uExtent / 0.6) * radius) - 1.0);
    return sign(coordinate) * mix(quadratic, focused, uNearMeshFocus);
}

vec3 surface(vec2 base, float time, float gridSpacing,
             out vec3 normal, out vec3 velocity) {
    vec3 position = vec3(base.x, uLevel, base.y);
    vec3 tangentX = vec3(1.0, 0.0, 0.0);
    vec3 tangentZ = vec3(0.0, 0.0, 1.0);
    velocity = vec3(0.0);
    vec2 warp = vec2(0.0);
    vec2 warpDx = vec2(0.0);
    vec2 warpDz = vec2(0.0);
    if (uWaveDiversity > 0.0) {
        float warpA = 0.037 * base.x + 0.071 * base.y;
        float warpB = 0.091 * base.x - 0.026 * base.y;
        float warpC = -0.055 * base.x + 0.041 * base.y;
        float warpD = 0.024 * base.x + 0.087 * base.y;
        warp = uWaveDiversity * vec2(
            2.8 * sin(warpA) + 1.3 * sin(warpB),
            2.1 * sin(warpC) + 1.1 * sin(warpD));
        warpDx = uWaveDiversity * vec2(
            2.8 * 0.037 * cos(warpA) + 1.3 * 0.091 * cos(warpB),
            -2.1 * 0.055 * cos(warpC) + 1.1 * 0.024 * cos(warpD));
        warpDz = uWaveDiversity * vec2(
            2.8 * 0.071 * cos(warpA) - 1.3 * 0.026 * cos(warpB),
            2.1 * 0.041 * cos(warpC) + 1.1 * 0.087 * cos(warpD));
    }
    for (int index = 0; index < uWaveCount; ++index) {
        vec4 wave = uWaves[index];
        // A wave shorter than two mesh edges aliases into large triangular
        // patches, especially when an open-ocean extent stretches this grid.
        float waveWeight = smoothstep(2.0, 4.0,
            wave.w / max(gridSpacing, 0.0001));
        if (waveWeight <= 0.0) continue;
        float k = 2.0 * PI / wave.w;
        float phaseSpeed = sqrt(9.81 / k) * uSpeed;
        float phase = k * (dot(wave.xy, base + warp) - phaseSpeed * time)
            + (index >= 4 ? float(index - 3) * 1.731 : 0.0);
        float phaseDx = k * (wave.x + dot(wave.xy, warpDx));
        float phaseDz = k * (wave.y + dot(wave.xy, warpDz));
        float sine = sin(phase);
        float cosine = cos(phase);
        float amplitude = wave.z * waveWeight;
        float horizontal = uSteepness * amplitude
            / (k * max(uWaves[0].z, 0.0001) * 4.0);
        position.xz += horizontal * wave.xy * cosine;
        position.y += amplitude * sine;
        tangentX += vec3(-horizontal * wave.x * phaseDx * sine,
                         amplitude * phaseDx * cosine,
                         -horizontal * wave.y * phaseDx * sine);
        tangentZ += vec3(-horizontal * wave.x * phaseDz * sine,
                         amplitude * phaseDz * cosine,
                         -horizontal * wave.y * phaseDz * sine);
        velocity += vec3(horizontal * wave.x * k * phaseSpeed * sine,
            -amplitude * k * phaseSpeed * cosine,
                         horizontal * wave.y * k * phaseSpeed * sine);
    }
    normal = normalize(cross(tangentZ, tangentX));
    return position;
}

void main() {
    float logicalStep = 2.0 / uGridResolution;
    float logicalRadius = max(abs(aLogicalPosition.x), abs(aLogicalPosition.y));
    float quadraticSpacing = uExtent * logicalStep
        * (2.0 * logicalRadius + logicalStep);
    float gridSpacing = quadraticSpacing;
    if (uNearMeshFocus > 0.0) {
        float focusedScale = log(1.0 + uExtent / 0.6);
        float focusedRadius = 0.6 * exp(focusedScale * logicalRadius);
        float focusedSpacing = focusedRadius * (exp(focusedScale * logicalStep) - 1.0);
        gridSpacing = mix(quadraticSpacing, focusedSpacing, uNearMeshFocus);
    }
    vec2 offset = vec2(gridCoordinate(aLogicalPosition.x),
                       gridCoordinate(aLogicalPosition.y));
    vec3 normal;
    vec3 velocity;
    vWorldPosition = surface(uCameraPosition.xz + offset, uTime,
        gridSpacing, normal, velocity);
    vNormal = normal;
    vVelocity = velocity;
    vFoam = clamp((1.0 - normal.y) * 3.0
        + max(vWorldPosition.y - uLevel, 0.0) / max(uWaves[0].z, 0.001) * 0.12,
        0.0, 1.0);
    vec3 previousNormal;
    vec3 previousVelocity;
    // Reproject the same world-space water patch. The camera-centered mesh is
    // only a sampling grid and must not move the patch in motion history.
    vec3 previousPosition = surface(uCameraPosition.xz + offset,
        uPreviousTime, gridSpacing, previousNormal, previousVelocity);
    vCurrentClip = uCurrentViewProjection * vec4(vWorldPosition, 1.0);
    vPreviousClip = uPreviousViewProjection * vec4(previousPosition, 1.0);
    vMotionValid = uMotionHistoryValid ? 1.0 : 0.0;
    gl_Position = vCurrentClip;
}
