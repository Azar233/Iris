#version 330 core
#include "cloud_shadow_sample.glsl"
#include "ocean_snoise.glsl"

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
uniform float uTime;
uniform float uRoughness;
uniform float uReflectionStrength;
uniform float uRippleStrength;
uniform float uSunGlintStrength;
uniform float uDeepWaterStrength;
uniform float uEnvironmentMaxMip;
uniform bool uShadowsEnabled;
uniform samplerCube uPrefilteredEnvironmentMap;
uniform samplerCube uIrradianceMap;
uniform sampler2DArrayShadow uShadowMap;
uniform sampler2D uOpaqueSceneColor;
uniform sampler2D uOpaqueSceneDepth;
uniform mat4 uCurrentViewProjection;
uniform mat4 uInverseCurrentViewProjection;
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
            visible += texture(uShadowMap, vec4(
                projected.xy + vec2(x, y) * texel, float(cascade), projected.z - bias));
        }
    }
    return visible / 9.0;
}

void main() {
    vec3 normal = normalize(vNormal);
    // Filter the two ripple scales by their projected pixel footprint. A fixed
    // world-space distance cutoff made nearby water busy and distant water flat.
    // Only shading normals change: Gerstner geometry and TAA motion stay stable.
    if (uRippleStrength > 0.0) {
        float pixelFootprint = max(length(dFdx(vWorldPosition.xz)),
                                   length(dFdy(vWorldPosition.xz)));
        float broadWeight = 1.0 - smoothstep(0.18, 0.45,
            pixelFootprint * 0.065);
        float detailWeight = 1.0 - smoothstep(0.18, 0.45,
            pixelFootprint * 0.28);
        vec2 slope = vec2(0.0);
        if (broadWeight > 0.0) {
            vec3 broadGradient;
            snoise3d(vec3(vWorldPosition.xz * 0.065, uTime * 0.09),
                broadGradient);
            slope += broadGradient.xy * 0.16 * broadWeight;
        }
        if (detailWeight > 0.0) {
            vec3 detailGradient;
            snoise3d(vec3(vWorldPosition.xz * 0.28, uTime * 0.19),
                detailGradient);
            slope += detailGradient.xy * 0.22 * detailWeight;
        }
        // Non-periodic microfacets break up the long Gerstner reflection
        // bands. Fade each octave once its world-space variation approaches
        // the projected pixel footprint, so distant water remains stable.
        float microWeight = 1.0 - smoothstep(0.18, 0.45,
            pixelFootprint * 0.9);
        if (microWeight > 0.0) {
            vec3 microGradient;
            snoise3d(vec3(vWorldPosition.xz * 0.9, uTime * 0.31),
                microGradient);
            slope += microGradient.xy * 0.34 * microWeight;
        }
        float capillaryWeight = 1.0 - smoothstep(0.18, 0.45,
            pixelFootprint * 2.1);
        if (capillaryWeight > 0.0) {
            vec3 capillaryGradient;
            snoise3d(vec3(vWorldPosition.xz * 2.1, uTime * 0.52),
                capillaryGradient);
            slope += capillaryGradient.xy * 0.12 * capillaryWeight;
        }
        slope *= uRippleStrength;
        normal = normalize(normal + vec3(-slope.x, 0.0, -slope.y));
    }
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    bool viewedFromBelow = uCameraPosition.y < vWorldPosition.y;
    if (dot(normal, viewDirection) < 0.0) normal = -normal;
    // Micro-normal changes finer than a pixel broaden the reflection lobe.
    // This reduces isolated HDRI sparkles without removing nearby resolved ripples.
    float normalVariance = max(dot(dFdx(normal), dFdx(normal)),
        dot(dFdy(normal), dFdy(normal)));
    float filteredRoughness = clamp(sqrt(uRoughness * uRoughness
        + 0.35 * normalVariance), 0.02, 0.8);
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
        reflect(-viewDirection, normal),
        clamp(filteredRoughness * uEnvironmentMaxMip, 0.0, uEnvironmentMaxMip)).rgb
        * uEnvironmentIntensity * uReflectionStrength;
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
        bool invalidRefraction = sceneDepth < 0.99999
            && viewDepth(sceneDepth) <= waterDepth + 0.05;
        if (!invalidRefraction && sceneDepth < 0.99999 && !viewedFromBelow) {
            vec4 candidate = uInverseCurrentViewProjection
                * vec4(refractedUv * 2.0 - 1.0, sceneDepth * 2.0 - 1.0, 1.0);
            // A displaced lookup may land on the dry side of a protruding object.
            // Its color must not be dragged into the water next to the silhouette.
            invalidRefraction = candidate.y / candidate.w > vWorldPosition.y + 0.05;
        }
        if (invalidRefraction) {
            refractedUv = screenUv;
            sceneDepth = initialSceneDepth;
        }
    }
    float thickness = sceneDepth >= 0.99999 ? 18.0
        : max(viewDepth(sceneDepth) - waterDepth, 0.0)
            / max(abs(dot(viewDirection, normalize(uCameraForward))), 0.25);
    // Below the surface the camera-to-interface path is fogged in postprocess;
    // everything behind that interface is air, not another 18 metres of water.
    thickness = viewedFromBelow ? 0.0 : clamp(thickness, 0.0, 18.0);
    vec3 absorption = vec3(0.32, 0.12, 0.065);
    vec3 transmittance = exp(-absorption * thickness);
    float cloudVisibility = cloudShadowTransmittance(vWorldPosition);
    // Keep the ambient floor; shadow the solar share of the artistic body color.
    vec3 subsurface = vec3(0.012, 0.085, 0.12) * (0.35 + 0.65 * cloudVisibility)
        + texture(uIrradianceMap, normal).rgb * 0.025 * uEnvironmentIntensity;
    subsurface *= uTwilightFactor;
    vec3 transmission = texture(uOpaqueSceneColor, refractedUv).rgb
        * transmittance + subsurface * (vec3(1.0) - transmittance);
    // A depth value of one means there is no refractive receiver below the
    // surface. Use an open-water radiance instead of refracting the skybox
    // through an arbitrary 18-metre layer. Existing scenes keep this off.
    if (sceneDepth >= 0.99999 && !viewedFromBelow) {
        vec3 deepWater = vec3(0.004, 0.025, 0.065) * uTwilightFactor
            + texture(uIrradianceMap, vec3(0.0, 1.0, 0.0)).rgb
                * 0.012 * uEnvironmentIntensity;
        transmission = mix(transmission, deepWater, uDeepWaterStrength);
    }
    float waterShadow = shadowVisibility(normal);
    transmission *= mix(0.55, 1.0, waterShadow);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float sunGlint = pow(max(dot(normal, halfDirection), 0.0),
            256.0 * exp2(-filteredRoughness * 4.0))
        * max(dot(normal, lightDirection), 0.0) * uDiffuseStrength
        * waterShadow * cloudVisibility;
    vec3 color = mix(transmission, reflection, fresnel)
        + uLightColor * sunGlint * 0.5 * uSunGlintStrength;
    float crestNoise = sin(vWorldPosition.x * 7.1 + vWorldPosition.z * 5.7)
        * sin(vWorldPosition.z * 4.3 - vWorldPosition.x * 3.9);
    float whitecap = smoothstep(0.62, 0.94, vFoam + 0.08 * crestNoise);
    // Contact foam follows the unwarped geometry depth, never a displaced
    // refraction lookup that could jump across an object's silhouette.
    float contactDepth = initialSceneDepth >= 0.99999 ? 18.0
        : initialThickness / max(abs(dot(viewDirection, normalize(uCameraForward))), 0.25);
    float shoreline = viewedFromBelow ? 0.0
        : (1.0 - smoothstep(0.02, 0.5, contactDepth))
        * (0.65 + 0.35 * crestNoise);
    float foam = clamp(max(whitecap, shoreline) * uFoamStrength, 0.0, 1.0);
    color = mix(color, vec3(0.68, 0.82, 0.86) * uTwilightFactor
        * (0.35 + 0.65 * cloudVisibility), foam);
    fragmentColor = vec4(color, 1.0);
}
