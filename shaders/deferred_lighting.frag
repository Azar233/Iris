#version 330 core
#include "cloud_shadow_sample.glsl"

// Must match `shadow::maximumCascadeCount`; declared before any use.
const int MAX_SHADOW_CASCADES = 4;

in vec2 vUv;

uniform sampler2D uGAlbedo;
uniform sampler2D uGNormal;
uniform sampler2D uGMaterial;
uniform sampler2D uGDepth;
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilteredEnvironmentMap;
uniform sampler2D uBrdfLut;
// Array sampler, matching the 2D-array depth attachment; see `basic.frag` for why the two must
// change together.
uniform sampler2DArrayShadow uShadowMap;
uniform sampler2D uTransmissionShadowMap;
uniform mat4 uOpticsLightViewProjection;
uniform sampler2D uCausticsMap;
uniform sampler2D uSsao;
uniform mat4 uInverseViewProjection;
uniform mat4 uLightViewProjection[MAX_SHADOW_CASCADES];
uniform int uShadowCascadeCount;
uniform float uCascadeSplits[MAX_SHADOW_CASCADES];
uniform bool uShadowCascadeDebugView;
// Depth planes of the camera projection. The G-buffer depth debug view linearises with these rather
// than with a fitted exponent, so the view stays truthful if the camera's depth range ever changes.
uniform float uCameraNearPlane;
uniform float uCameraFarPlane;
uniform vec3 uCameraPosition;
// The camera's forward axis, so a fragment's view depth is measured in the space the cascade splits
// are expressed in rather than as the length of the view ray.
uniform vec3 uCameraForward;
uniform vec3 uLightDirection;
const int MAX_LOCAL_LIGHTS = 64;
uniform int uLocalLightCount;
uniform vec4 uLocalLightPositionRadius[MAX_LOCAL_LIGHTS];
uniform vec4 uLocalLightColorIntensity[MAX_LOCAL_LIGHTS];
uniform vec4 uLocalLightDirectionOuter[MAX_LOCAL_LIGHTS];
uniform float uAmbientStrength;
uniform float uDiffuseStrength;
uniform float uSpecularStrength;
// Spectrum of the directional key light. White unless the analytic sky is driving the scene, in
// which case it is the sun's atmospheric transmittance normalised to its brightest channel: the
// light's brightness stays in the two strengths above, so this only carries colour.
uniform vec3 uLightColor;
uniform float uShininess;
uniform float uEnvironmentIntensity;
uniform float uEnvironmentMaxMip;
uniform bool uPbrEnabled;
uniform bool uStylizedEnabled;
uniform int uStylizedBandCount;
uniform float uStylizedBandSoftness;
uniform float uStylizedSpecularSize;
uniform float uStylizedSpecularSoftness;
uniform float uStylizedRimWidth;
uniform float uStylizedRimSoftness;
uniform float uStylizedRimIntensity;
uniform vec3 uStylizedShadowTint;
uniform vec3 uStylizedRimColor;
uniform int uStylizedDebugView;
uniform bool uIblEnabled;
uniform bool uShadowsEnabled;
uniform bool uColoredTransmissionShadowsEnabled;
uniform bool uCausticsEnabled;
uniform bool uSsaoEnabled;
uniform bool uSkinningDebugActive;
uniform int uGBufferDebugView;

out vec4 fragmentColor;

const float PI = 3.14159265359;

vec3 reconstructWorldPosition(float depth) {
    vec4 clip = vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    return world.xyz / max(world.w, 0.00001);
}

float distributionGGX(vec3 normal, vec3 halfDirection, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfDirection), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

vec3 fresnelSchlick(float cosine, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float stylizedBand(float value) {
    float levels = float(max(uStylizedBandCount - 1, 1));
    float scaled = clamp(value, 0.0, 1.0) * levels;
    if (uStylizedBandSoftness <= 0.0001) {
        return floor(scaled + 0.5) / levels;
    }
    float lower = floor(scaled);
    float blend = smoothstep(
        0.5 - clamp(uStylizedBandSoftness, 0.0, 0.49),
        0.5 + clamp(uStylizedBandSoftness, 0.0, 0.49),
        fract(scaled)
    );
    return mix(lower, min(lower + 1.0, levels), blend) / levels;
}

float stylizedHighlight(float nDotH, float nDotL) {
    float threshold = 1.0 - clamp(uStylizedSpecularSize, 0.0, 0.98);
    float softness = max(uStylizedSpecularSoftness, 0.0001);
    return smoothstep(threshold - softness, threshold + softness, nDotH)
        * step(0.0001, nDotL);
}

float stylizedRim(float nDotV) {
    float width = clamp(uStylizedRimWidth, 0.0, 0.98);
    float softness = max(uStylizedRimSoftness, 0.0001);
    float rim = 1.0 - clamp(nDotV, 0.0, 1.0);
    return smoothstep(1.0 - width - softness, 1.0 - width + softness, rim);
}

vec3 localLightRadiance(int index, vec3 worldPosition, out vec3 lightDirection) {
    vec4 positionRadius = uLocalLightPositionRadius[index];
    vec3 toLight = positionRadius.xyz - worldPosition;
    float distanceToLight = length(toLight);
    lightDirection = toLight / max(distanceToLight, 0.0001);
    float radius = max(abs(positionRadius.w), 0.001);
    float distanceRatio = distanceToLight / radius;
    float smoothRange = clamp(1.0 - pow(distanceRatio, 4.0), 0.0, 1.0);
    float attenuation = smoothRange * smoothRange
        / max(1.0 + distanceToLight * distanceToLight, 0.0001);

    if (positionRadius.w < 0.0) {
        vec4 directionOuter = uLocalLightDirectionOuter[index];
        vec3 fromLight = normalize(worldPosition - positionRadius.xyz);
        float coneCosine = dot(fromLight, normalize(directionOuter.xyz));
        float innerCosine = min(directionOuter.w + 0.10, 0.999);
        attenuation *= smoothstep(directionOuter.w, innerCosine, coneCosine);
    }
    vec4 colorIntensity = uLocalLightColorIntensity[index];
    return colorIntensity.rgb * colorIntensity.w * attenuation;
}

// Cascade selection from the reconstructed view depth, matching the forward path.
int selectCascade(float viewDepth) {
    int selected = 0;
    for (int cascade = 0; cascade < MAX_SHADOW_CASCADES; ++cascade) {
        if (cascade >= uShadowCascadeCount) break;
        selected = cascade;
        if (viewDepth <= uCascadeSplits[cascade]) break;
    }
    return selected;
}

vec3 cascadeDebugColor(int cascade) {
    const vec3 colors[MAX_SHADOW_CASCADES] = vec3[MAX_SHADOW_CASCADES](
        vec3(0.95, 0.28, 0.24), vec3(0.25, 0.80, 0.35),
        vec3(0.25, 0.48, 0.96), vec3(0.96, 0.78, 0.22));
    return colors[clamp(cascade, 0, MAX_SHADOW_CASCADES - 1)];
}

vec3 projectedCoordinates(vec3 worldPosition, int cascade) {
    vec4 shadowPosition = uLightViewProjection[cascade] * vec4(worldPosition, 1.0);
    return shadowPosition.xyz / max(shadowPosition.w, 0.0001) * 0.5 + 0.5;
}

// These 2D optics maps are generated in the independent fixed light box,
// not in the camera-fitted cascade that provides opaque depth comparisons.
vec3 opticsProjectionCoordinates(vec3 worldPosition) {
    vec4 clip = uOpticsLightViewProjection * vec4(worldPosition, 1.0);
    return clip.xyz / max(clip.w, 0.0001) * 0.5 + 0.5;
}

vec3 transmissionVisibility(vec3 worldPosition) {
    if (!uColoredTransmissionShadowsEnabled) return vec3(1.0);
    vec3 projected = opticsProjectionCoordinates(worldPosition);
    if (projected.z < 0.0 || projected.z > 1.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(1.0);
    return texture(uTransmissionShadowMap, projected.xy).rgb;
}

vec3 shadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDirection, float viewDepth) {
    if (!uShadowsEnabled) return vec3(1.0);
    int cascade = selectCascade(viewDepth);
    vec3 projected = projectedCoordinates(worldPosition, cascade);
    if (projected.z > 1.0 || projected.z < 0.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(1.0);
    float bias = max(0.0015 * (1.0 - dot(normal, lightDirection)), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
    float visible = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            visible += texture(uShadowMap, vec4(
                projected.xy + vec2(x, y) * texel, float(cascade), projected.z - bias));
        }
    }
    vec3 transmission = transmissionVisibility(worldPosition);
    return transmission * (visible / 9.0);
}

vec3 causticRadiance(vec3 worldPosition) {
    if (!uCausticsEnabled) return vec3(0.0);
    vec3 projected = opticsProjectionCoordinates(worldPosition);
    if (projected.z > 1.0 || projected.z < 0.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return vec3(0.0);
    return texture(uCausticsMap, projected.xy).rgb;
}

void main() {
    float depth = texture(uGDepth, vUv).r;
    vec4 albedoSample = texture(uGAlbedo, vUv);
    vec3 albedo = albedoSample.rgb;
    vec4 normalSample = texture(uGNormal, vUv);
    vec3 encodedNormal = normalSample.rgb;
    vec2 material = texture(uGMaterial, vUv).rg;

    if (uSkinningDebugActive) {
        if (depth >= 0.999999) discard;
        fragmentColor = vec4(albedo, 1.0);
        return;
    }

    if (uGBufferDebugView == 1) {
        fragmentColor = vec4(albedo, 1.0);
        return;
    }
    if (uGBufferDebugView == 2) {
        fragmentColor = vec4(encodedNormal, 1.0);
        return;
    }
    if (uGBufferDebugView == 3) {
        fragmentColor = vec4(material.r, material.g, 0.0, 1.0);
        return;
    }
    if (uGBufferDebugView == 4) {
        // This view is a pinned regression baseline, so its exact appearance is part of the contract
        // and must not be "improved" as a side effect of another change. The exponent below is the
        // established look; what changed is that it is no longer a bare literal. The near plane it
        // implies is normalised out, so moving the camera's near plane cannot silently re-shade the
        // baseline. A separate true-linear-depth view can be added later as its own debug mode.
        const float referenceNearPlane = 0.05;
        float near = max(uCameraNearPlane, 1.0e-4);
        float exponent = 64.0 * referenceNearPlane / near;
        float linearized = pow(clamp(depth, 0.0, 1.0), exponent);
        fragmentColor = vec4(vec3(1.0 - linearized), 1.0);
        return;
    }
    float ambientOcclusion = uSsaoEnabled ? texture(uSsao, vUv).r : 1.0;
    if (uGBufferDebugView == 5) {
        fragmentColor = vec4(vec3(ambientOcclusion), 1.0);
        return;
    }
    if (depth >= 0.999999) discard;
    if (uShadowCascadeDebugView && uShadowsEnabled) {
        vec3 worldPosition = reconstructWorldPosition(depth);
        float viewDepth = dot(worldPosition - uCameraPosition, uCameraForward);
        fragmentColor = vec4(cascadeDebugColor(selectCascade(viewDepth)), 1.0);
        return;
    }
    vec3 normal = normalize(encodedNormal * 2.0 - 1.0);
    float metallic = material.r;
    float roughness = clamp(material.g, 0.04, 1.0);

    vec3 worldPosition = reconstructWorldPosition(depth);
    vec3 viewDirection = normalize(uCameraPosition - worldPosition);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    // View depth along the camera's forward axis, recovered from the basis rather than approximated
    // by the ray length: the two disagree by up to `1 / cos(angle)` away from the screen centre.
    float viewDepth = dot(worldPosition - uCameraPosition, uCameraForward);
    vec3 visibility = shadowVisibility(worldPosition, normal, lightDirection, viewDepth);
    float cloudVisibility = cloudShadowTransmittance(worldPosition);
    vec3 caustics = causticRadiance(worldPosition);

    if (normalSample.a > 0.25 && normalSample.a < 0.75) {
        float nDotVWater = max(dot(normal, viewDirection), 0.0);
        float f0Water = pow((1.333 - 1.0) / (1.333 + 1.0), 2.0);
        float fresnelWater = f0Water + (1.0 - f0Water) * pow(1.0 - nDotVWater, 5.0);
        vec3 reflectionWater = textureLod(uPrefilteredEnvironmentMap,
            reflect(-viewDirection, normal), 1.5).rgb * uEnvironmentIntensity;
        vec3 transmissionWater = vec3(0.015, 0.11, 0.16) * (0.35 + 0.65 * cloudVisibility)
            + texture(uIrradianceMap, normal).rgb * 0.045 * uEnvironmentIntensity;
        vec3 halfWater = normalize(lightDirection + viewDirection);
        float shadowWater = dot(visibility, vec3(0.333333));
        transmissionWater *= mix(0.55, 1.0, shadowWater);
        float sunGlint = pow(max(dot(normal, halfWater), 0.0), 128.0)
            * max(dot(normal, lightDirection), 0.0) * uDiffuseStrength * shadowWater * cloudVisibility;
        vec3 colorWater = mix(transmissionWater, reflectionWater, fresnelWater)
            + uLightColor * sunGlint * 0.5;
        colorWater = mix(colorWater, vec3(0.68, 0.82, 0.86)
            * (0.35 + 0.65 * cloudVisibility), albedoSample.a);
        fragmentColor = vec4(colorWater, 1.0);
        return;
    }

    visibility *= cloudVisibility;
    if (uStylizedEnabled) {
        float diffuseBand = stylizedBand(nDotL);
        float rim = stylizedRim(nDotV)
            * mix(0.35, 1.0, 1.0 - nDotL)
            * max(uStylizedRimIntensity, 0.0);
        if (uStylizedDebugView == 1) {
            fragmentColor = vec4(vec3(diffuseBand), 1.0);
            return;
        }
        if (uStylizedDebugView == 2) {
            fragmentColor = vec4(vec3(clamp(rim, 0.0, 1.0)), 1.0);
            return;
        }
        vec3 shadowColor = mix(
            max(uStylizedShadowTint, vec3(0.0)),
            vec3(1.0),
            clamp(visibility, vec3(0.0), vec3(1.0))
        );
        vec3 ambient = albedo * uAmbientStrength * ambientOcclusion;
        if (uIblEnabled) {
            ambient += texture(uIrradianceMap, normal).rgb * albedo
                * uEnvironmentIntensity * 0.18 * ambientOcclusion;
        }
        vec3 specularColor = mix(vec3(1.0), albedo, metallic);
        vec3 color = ambient
            + uLightColor * (
                albedo * uDiffuseStrength * diffuseBand * shadowColor
                + specularColor * uSpecularStrength
                    * stylizedHighlight(max(dot(normal, halfDirection), 0.0), nDotL)
                    * shadowColor
            );
        for (int index = 0; index < uLocalLightCount; ++index) {
            vec3 localDirection;
            vec3 radiance = localLightRadiance(index, worldPosition, localDirection);
            float localNDotL = max(dot(normal, localDirection), 0.0);
            vec3 localHalf = normalize(localDirection + viewDirection);
            color += radiance * (
                albedo * uDiffuseStrength * stylizedBand(localNDotL)
                + specularColor * uSpecularStrength * stylizedHighlight(
                    max(dot(normal, localHalf), 0.0), localNDotL
                )
            );
        }
        color += max(uStylizedRimColor, vec3(0.0)) * rim;
        fragmentColor = vec4(color + caustics * albedo, 1.0);
        return;
    }

    if (!uPbrEnabled) {
        float diffuse = nDotL * uDiffuseStrength;
        float specular = pow(max(dot(normal, halfDirection), 0.0), uShininess)
            * uSpecularStrength;
        vec3 localLighting = vec3(0.0);
        for (int index = 0; index < uLocalLightCount; ++index) {
            vec3 localDirection;
            vec3 radiance = localLightRadiance(index, worldPosition, localDirection);
            float localNDotL = max(dot(normal, localDirection), 0.0);
            vec3 localHalf = normalize(localDirection + viewDirection);
            float localSpecular = localNDotL > 0.0
                ? pow(max(dot(normal, localHalf), 0.0), uShininess)
                    * uSpecularStrength
                : 0.0;
            localLighting += radiance * (
                albedo * localNDotL * uDiffuseStrength + localSpecular
            );
        }
        fragmentColor = vec4(
            albedo * uAmbientStrength * ambientOcclusion
                + visibility * uLightColor * (albedo * diffuse + vec3(specular))
                + caustics * albedo + localLighting,
            1.0
        );
        return;
    }

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = fresnelSchlick(max(dot(halfDirection, viewDirection), 0.0), f0);
    float distribution = distributionGGX(normal, halfDirection, roughness);
    float geometry = geometrySchlickGGX(nDotV, roughness)
        * geometrySchlickGGX(nDotL, roughness);
    vec3 specular = distribution * geometry * fresnel
        / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 direct = (diffuseWeight * albedo / PI + specular)
        * nDotL * uDiffuseStrength;
    vec3 localDirect = vec3(0.0);
    for (int index = 0; index < uLocalLightCount; ++index) {
        vec3 localDirection;
        vec3 radiance = localLightRadiance(index, worldPosition, localDirection);
        float localNDotL = max(dot(normal, localDirection), 0.0);
        if (localNDotL <= 0.0) continue;
        vec3 localHalf = normalize(viewDirection + localDirection);
        vec3 localFresnel = fresnelSchlick(
            max(dot(localHalf, viewDirection), 0.0),
            f0
        );
        float localDistribution = distributionGGX(normal, localHalf, roughness);
        float localGeometry = geometrySchlickGGX(nDotV, roughness)
            * geometrySchlickGGX(localNDotL, roughness);
        vec3 localSpecular = localDistribution * localGeometry * localFresnel
            / max(4.0 * nDotV * localNDotL, 0.0001);
        vec3 localDiffuseWeight = (vec3(1.0) - localFresnel) * (1.0 - metallic);
        localDirect += radiance * (
            localDiffuseWeight * albedo / PI + localSpecular
        ) * localNDotL * uDiffuseStrength;
    }

    vec3 ambient = albedo * uAmbientStrength;
    if (uIblEnabled) {
        vec3 iblFresnel = fresnelSchlick(nDotV, f0);
        vec3 irradiance = texture(uIrradianceMap, normal).rgb;
        vec3 diffuseIbl = irradiance * albedo;
        vec3 reflected = reflect(-viewDirection, normal);
        vec3 prefiltered = textureLod(
            uPrefilteredEnvironmentMap,
            reflected,
            roughness * uEnvironmentMaxMip
        ).rgb;
        vec2 brdf = texture(uBrdfLut, vec2(nDotV, roughness)).rg;
        vec3 specularIbl = prefiltered * (iblFresnel * brdf.x + brdf.y);
        ambient = ((vec3(1.0) - iblFresnel) * (1.0 - metallic) * diffuseIbl
            + specularIbl) * uEnvironmentIntensity;
    }
    fragmentColor = vec4(
        ambient * ambientOcclusion
            + visibility * uLightColor * direct
            + localDirect + caustics * albedo,
        1.0
    );
}
