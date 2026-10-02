#version 330 core

layout (location = 0) in vec2 aLogicalPosition;

uniform mat4 uCurrentViewProjection;
uniform mat4 uPreviousViewProjection;
uniform vec3 uCameraPosition;
uniform float uExtent;
uniform float uGridResolution;
uniform float uLevel;
uniform float uTime;
uniform float uPreviousTime;
uniform float uSpeed;
uniform float uSteepness;
uniform vec4 uWaves[4]; // direction.xy, amplitude, wavelength
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
    return sign(coordinate) * coordinate * coordinate * uExtent;
}

vec3 surface(vec2 base, float time, float gridSpacing,
             out vec3 normal, out vec3 velocity) {
    vec3 position = vec3(base.x, uLevel, base.y);
    vec3 tangentX = vec3(1.0, 0.0, 0.0);
    vec3 tangentZ = vec3(0.0, 0.0, 1.0);
    velocity = vec3(0.0);
    for (int index = 0; index < uWaveCount; ++index) {
        vec4 wave = uWaves[index];
        // A wave shorter than two mesh edges aliases into large triangular
        // patches, especially when an open-ocean extent stretches this grid.
        float waveWeight = smoothstep(2.0, 4.0,
            wave.w / max(gridSpacing, 0.0001));
        if (waveWeight <= 0.0) continue;
        float k = 2.0 * PI / wave.w;
        float phaseSpeed = sqrt(9.81 / k) * uSpeed;
        float phase = k * (dot(wave.xy, base) - phaseSpeed * time);
        float sine = sin(phase);
        float cosine = cos(phase);
        float amplitude = wave.z * waveWeight;
        float horizontal = uSteepness * amplitude
            / (k * max(uWaves[0].z, 0.0001) * 4.0);
        position.xz += horizontal * wave.xy * cosine;
        position.y += amplitude * sine;
        tangentX += vec3(-horizontal * k * wave.x * wave.x * sine,
                         amplitude * k * wave.x * cosine,
                         -horizontal * k * wave.x * wave.y * sine);
        tangentZ += vec3(-horizontal * k * wave.x * wave.y * sine,
                         amplitude * k * wave.y * cosine,
                         -horizontal * k * wave.y * wave.y * sine);
        velocity += vec3(horizontal * wave.x * k * phaseSpeed * sine,
            -amplitude * k * phaseSpeed * cosine,
                         horizontal * wave.y * k * phaseSpeed * sine);
    }
    normal = normalize(cross(tangentZ, tangentX));
    return position;
}

void main() {
    float logicalStep = 2.0 / uGridResolution;
    float gridSpacing = uExtent * logicalStep
        * (2.0 * max(abs(aLogicalPosition.x), abs(aLogicalPosition.y))
            + logicalStep);
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
