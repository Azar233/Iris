#version 330 core

in vec2 vUv;
uniform samplerCube uEnvironmentMap;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform float uEnvironmentIntensity;
uniform bool uAnalyticSun;
uniform vec3 uSunDirection;
uniform vec3 uSunRadiance;
uniform float uSunAngularRadius;
out vec4 fragmentColor;

void main() {
    vec4 world = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w - uCameraPosition);
    vec3 radiance = textureLod(uEnvironmentMap, direction, 0.0).rgb;
    if (uAnalyticSun) {
        // Evaluate the disk at pixel resolution rather than magnifying a few
        // baked cube texels. The cross product stays precise near the disk centre.
        float angularDistance = length(cross(direction, uSunDirection));
        float radius = sin(uSunAngularRadius);
        float footprint = max(length(dFdx(direction)), length(dFdy(direction)));
        float coverage = 1.0 - smoothstep(radius - footprint * 0.5,
            radius + footprint * 0.5, angularDistance);
        if (dot(direction, uSunDirection) > 0.0) radiance += uSunRadiance * coverage;
    }
    fragmentColor = vec4(radiance * uEnvironmentIntensity, 1.0);
}
