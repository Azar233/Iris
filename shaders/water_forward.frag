#version 330 core
#include "cloud_shadow_sample.glsl"

in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vVelocity;
in float vFoam;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
in float vMotionValid;

uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uDiffuseStrength;
uniform float uEnvironmentIntensity;
uniform float uTwilightFactor;
uniform float uFoamStrength;
uniform bool uShadowsEnabled;
uniform samplerCube uPrefilteredEnvironmentMap;
uniform samplerCube uIrradianceMap;
uniform sampler2DArray uShadowMap;
uniform sampler2D uOpaqueSceneColor;
uniform sampler2D uOpaqueSceneDepth;
uniform mat4 uCurrentViewProjection;
uniform float uInverseViewportWidth;
uniform float uInverseViewportHeight;
uniform float uCameraNearPlane;
uniform float uCameraFarPlane;
uniform bool uHighQuality;
uniform mat4 uLightViewProjection[4];
uniform float uCascadeSplits[4];
uniform int uShadowCascadeCount;
uniform vec3 uCameraForward;

out vec4 fragmentColor;

float viewDepth(float deviceDepth) {
    float ndc = deviceDepth * 2.0 - 1.0;
    return 2.0 * uCameraNearPlane * uCameraFarPlane
        / max(uCameraFarPlane + uCameraNearPlane
            - ndc * (uCameraFarPlane - uCameraNearPlane), 0.0001);
}

float shadowVisibility(vec3 normal) {
    if (!uShadowsEnabled) return 1.0;
    float depth = dot(vWorldPosition - uCameraPosition, uCameraForward);
    int cascade = 0;
    for (int index = 0; index < 4; ++index) {
        if (index >= uShadowCascadeCount) break;
        cascade = index;
        if (depth <= uCascadeSplits[index]) break;
    }
    vec4 lightClip = uLightViewProjection[cascade] * vec4(vWorldPosition, 1.0);
    vec3 projected = lightClip.xyz / max(lightClip.w, 0.0001) * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return 1.0;
    float bias = max(0.0015 * (1.0 - dot(normal, normalize(-uLightDirection))), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
    float visible = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float stored = texture(uShadowMap,
                vec3(projected.xy + vec2(x, y) * texel, cascade)).r;
            visible += projected.z - bias <= stored ? 1.0 : 0.0;
        }
    }
    return visible / 9.0;
}

void main() {
    vec3 normal = normalize(vNormal);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    bool viewedFromBelow = dot(normal, viewDirection) < 0.0;
    if (viewedFromBelow) normal = -normal;
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float etaIncident = viewedFromBelow ? 1.333 : 1.0;
    float etaTransmit = viewedFromBelow ? 1.0 : 1.333;
    float eta = etaIncident / etaTransmit;
    float sinSquaredTransmit = eta * eta * (1.0 - nDotV * nDotV);
    float fresnel = 1.0;
    if (sinSquaredTransmit < 1.0) {
        float cosTransmit = sqrt(1.0 - sinSquaredTransmit);
        float perpendicular = (etaIncident * nDotV - etaTransmit * cosTransmit)
            / max(etaIncident * nDotV + etaTransmit * cosTransmit, 0.0001);
        float parallel = (etaTransmit * nDotV - etaIncident * cosTransmit)
            / max(etaTransmit * nDotV + etaIncident * cosTransmit, 0.0001);
        fresnel = 0.5 * (perpendicular * perpendicular + parallel * parallel);
    }
    vec3 reflection = textureLod(uPrefilteredEnvironmentMap,
        reflect(-viewDirection, normal), 1.5).rgb * uEnvironmentIntensity;
    vec2 screenUv = gl_FragCoord.xy
        * vec2(uInverseViewportWidth, uInverseViewportHeight);
    float waterDepth = viewDepth(gl_FragCoord.z);
    float initialSceneDepth = texture(uOpaqueSceneDepth, screenUv).r;
    float initialThickness = initialSceneDepth >= 0.99999 ? 18.0
        : max(viewDepth(initialSceneDepth) - waterDepth, 0.0);
    vec2 refractedUv = screenUv;
    float sceneDepth = initialSceneDepth;
    if (uHighQuality) {
        vec3 refractedRay = refract(-viewDirection, normal, eta);
        vec4 refractedClip = uCurrentViewProjection
            * vec4(vWorldPosition + refractedRay * min(initialThickness, 2.0), 1.0);
        refractedUv = clamp(refractedClip.xy / max(refractedClip.w, 0.0001)
            * 0.5 + 0.5, vec2(0.001), vec2(0.999));
        refractedUv = screenUv + clamp(refractedUv - screenUv,
            vec2(-0.035), vec2(0.035));
        sceneDepth = texture(uOpaqueSceneDepth, refractedUv).r;
        if (sceneDepth < gl_FragCoord.z + 0.0005) {
            refractedUv = screenUv;
            sceneDepth = initialSceneDepth;
        }
    }
    float thickness = sceneDepth >= 0.99999 ? 18.0
        : max(viewDepth(sceneDepth) - waterDepth, 0.0)
            / max(abs(dot(viewDirection, normalize(uCameraForward))), 0.25);
    thickness = clamp(thickness, 0.0, 18.0);
    vec3 absorption = vec3(0.32, 0.12, 0.065);
    vec3 transmittance = exp(-absorption * thickness);
    float cloudVisibility = cloudShadowTransmittance(vWorldPosition);
    // Keep the ambient floor; shadow the solar share of the artistic body color.
    vec3 subsurface = vec3(0.012, 0.085, 0.12) * (0.35 + 0.65 * cloudVisibility)
        + texture(uIrradianceMap, normal).rgb * 0.025 * uEnvironmentIntensity;
    subsurface *= uTwilightFactor;
    vec3 transmission = texture(uOpaqueSceneColor, refractedUv).rgb
        * transmittance + subsurface * (vec3(1.0) - transmittance);
    float waterShadow = shadowVisibility(normal);
    transmission *= mix(0.55, 1.0, waterShadow);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float sunGlint = pow(max(dot(normal, halfDirection), 0.0), 128.0)
        * max(dot(normal, lightDirection), 0.0) * uDiffuseStrength
        * waterShadow * cloudVisibility;
    vec3 color = mix(transmission, reflection, fresnel)
        + uLightColor * sunGlint * 0.5;
    float crestNoise = sin(vWorldPosition.x * 7.1 + vWorldPosition.z * 5.7)
        * sin(vWorldPosition.z * 4.3 - vWorldPosition.x * 3.9);
    float whitecap = smoothstep(0.62, 0.94, vFoam + 0.08 * crestNoise);
    float shoreline = (1.0 - smoothstep(0.08, 1.1, thickness))
        * (0.65 + 0.35 * crestNoise);
    float foam = clamp(max(whitecap, shoreline) * uFoamStrength, 0.0, 1.0);
    color = mix(color, vec3(0.68, 0.82, 0.86) * uTwilightFactor
        * (0.35 + 0.65 * cloudVisibility), foam);
    fragmentColor = vec4(color, 1.0);
}
