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
uniform bool uSurfaceOptics;
uniform float uCameraSurfaceHeight;
uniform bool uSeparatedWaterSun;
uniform vec3 uWaterKeyIrradiance;
uniform bool uCloudReflectionEnabled;
uniform float uCloudReflectionStrength;
uniform sampler2D uCloudReflection;
uniform sampler2D uWaterBrdfLut;
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
    float unresolvedVariance = 0.0;
    // Filter the two ripple scales by their projected pixel footprint. A fixed
    // world-space distance cutoff made nearby water busy and distant water flat.
    // Only shading normals change: Gerstner geometry and TAA motion stay stable.
    if (uRippleStrength > 0.0) {
        float pixelFootprint = max(length(dFdx(vWorldPosition.xz)),
                                   length(dFdy(vWorldPosition.xz)));
        if (uSurfaceOptics) {
            // Incommensurate wind ripples carry resolved centimetre-to-metre
            // structure. Unresolved slope energy broadens the reflection lobe.
            vec2 slope = vec2(0.0);
            float wavelength = 2.8;
            float amplitude = 0.23;
            for (int octave = 0; octave < 9; ++octave) {
                float angle = float(octave) * 2.399963 + 0.31;
                vec2 direction = vec2(cos(angle), sin(angle));
                float k = 6.2831853 / wavelength;
                float weight = 1.0 - smoothstep(0.6, 2.2, k * pixelFootprint);
                float harmonicWeight = 1.0 - smoothstep(0.6, 2.2, 2.0 * k * pixelFootprint);
                float phase = k * dot(vWorldPosition.xz, direction)
                    - sqrt(9.81 * k) * uTime + float(octave) * 1.731;
                slope += direction * amplitude
                    * (weight * cos(phase) + 0.35 * harmonicWeight * cos(2.0 * phase));
                unresolvedVariance += amplitude * amplitude
                    * (0.5 * (1.0 - weight) + 0.06125 * (1.0 - harmonicWeight));
                wavelength *= 0.62;
                amplitude *= 0.86;
            }
            vec2 macroSlope = -normal.xz / max(normal.y, 0.2);
            vec2 totalSlope = macroSlope + slope * uRippleStrength;
            normal = normalize(vec3(-totalSlope.x, 1.0, -totalSlope.y));
        } else {
            float broadWeight = 1.0 - smoothstep(0.18, 0.45,
                pixelFootprint * 0.065);
            float detailWeight = 1.0 - smoothstep(0.18, 0.45,
                pixelFootprint * 0.28);
            unresolvedVariance += 0.0256 * (1.0 - broadWeight) + 0.0484 * (1.0 - detailWeight);
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
            unresolvedVariance += 0.1156 * (1.0 - microWeight);
            if (microWeight > 0.0) {
                vec3 microGradient;
                snoise3d(vec3(vWorldPosition.xz * 0.9, uTime * 0.31),
                    microGradient);
                slope += microGradient.xy * 0.34 * microWeight;
            }
            float capillaryWeight = 1.0 - smoothstep(0.18, 0.45,
                pixelFootprint * 2.1);
            unresolvedVariance += 0.0144 * (1.0 - capillaryWeight);
            if (capillaryWeight > 0.0) {
                vec3 capillaryGradient;
                snoise3d(vec3(vWorldPosition.xz * 2.1, uTime * 0.52),
                    capillaryGradient);
                slope += capillaryGradient.xy * 0.12 * capillaryWeight;
            }
            // Noise derivatives are slopes, not unit normals. Bound their artistic
            // scale so the extra detail does not fold every wave into grazing facets.
            slope *= uRippleStrength * (uSurfaceOptics ? 0.5 : 1.0);
            normal = normalize(normal + vec3(-slope.x, 0.0, -slope.y));
            }
    }
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float continuousVariance = max(dot(dFdx(normal), dFdx(normal)), dot(dFdy(normal), dFdy(normal)));
    bool viewedFromBelow = uSurfaceOptics ? uCameraPosition.y < uCameraSurfaceHeight
        : uCameraPosition.y < vWorldPosition.y;
    if (dot(normal, viewDirection) < 0.0) normal = -normal;
    // Micro-normal changes finer than a pixel broaden the reflection lobe.
    // This reduces isolated HDRI sparkles without removing nearby resolved ripples.
    float normalVariance = max(dot(dFdx(normal), dFdx(normal)),
        dot(dFdy(normal), dFdy(normal)));
    if (uSurfaceOptics) normalVariance = continuousVariance;
    float filteredRoughness = clamp(sqrt(uRoughness * uRoughness
        + 0.35 * normalVariance + (uSurfaceOptics ? unresolvedVariance * uRippleStrength * uRippleStrength : 0.0)), 0.02, 0.8);
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
    vec3 reflectedDirection = reflect(-viewDirection, normal);
    vec3 reflection = textureLod(uPrefilteredEnvironmentMap,
        reflectedDirection,
        clamp(filteredRoughness * uEnvironmentMaxMip, 0.0, uEnvironmentMaxMip)).rgb
        * uEnvironmentIntensity * uReflectionStrength;
    float reflectance = fresnel;
    if (uSurfaceOptics && sinSquaredTransmit < 1.0) {
        // Split-sum visibility bounds grazing IBL energy; a raw Fresnel mix
        // ignored masking and turned the solar environment into white ribbons.
        vec2 brdf = texture(uWaterBrdfLut, vec2(nDotV, filteredRoughness)).rg;
        reflectance = clamp(0.02037 * brdf.x + brdf.y, 0.0, 1.0);
    }
    if (uSurfaceOptics && uCloudReflectionEnabled && !viewedFromBelow && reflectedDirection.y > 0.0) {
        vec4 clip = uCurrentViewProjection * vec4(reflectedDirection, 0.0);
        vec2 uv = clip.xy / max(clip.w, 1.0e-6) * 0.5 + 0.5;
        float border = min(min(uv.x, uv.y), min(1.0-uv.x, 1.0-uv.y));
        if (clip.w > 0.0 && border > 0.0 && texture(uOpaqueSceneDepth, uv).r >= 0.99999) {
            vec2 radius = vec2(uInverseViewportWidth, uInverseViewportHeight)
                * (2.0 + filteredRoughness * 16.0);
            vec3 radiance = vec3(0.0);
            for (int i = 0; i < 4; ++i) {
                vec2 delta = vec2((i & 1) == 0 ? -1.0 : 1.0, i < 2 ? -1.0 : 1.0) * radius;
                vec2 sampleUv = clamp(uv + delta, vec2(0.001), vec2(0.999));
                vec4 cloud = texture(uCloudReflection, sampleUv);
                // Only sky samples may enter this supplement; nearby opaque
                // silhouettes use the independent environment fallback.
                bool skySample = texture(uOpaqueSceneDepth, sampleUv).r >= 0.99999;
                // Preserve the roughness-filtered sky lobe. Sampling the raw
                // sky image here would reintroduce a sharp solar disk even on
                // rough water, creating thin white contour bands.
                vec3 sky = textureLod(uPrefilteredEnvironmentMap, reflectedDirection,
                    filteredRoughness * uEnvironmentMaxMip).rgb * uEnvironmentIntensity;
                radiance += skySample ? cloud.rgb + sky * cloud.a : sky;
            }
            reflection = mix(reflection, radiance * (0.25 * uReflectionStrength),
                uCloudReflectionStrength * smoothstep(0.0,0.035,border));
        }
    }
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
        vec3 deepWater = (uSurfaceOptics ? vec3(0.012, 0.028, 0.042)
            : vec3(0.004, 0.025, 0.065)) * uTwilightFactor
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
    vec3 directSpecular = uLightColor * sunGlint * 0.5 * uSunGlintStrength;
    if (uSeparatedWaterSun) {
        // The tiny solar disk is integrated analytically. A 96-sample cubemap
        // convolution rarely hits it and produces isolated, extremely bright texels.
        float alpha = max(filteredRoughness * filteredRoughness, 0.04);
        float a2 = alpha * alpha;
        float nDotH = max(dot(normal, halfDirection), 0.0);
        float nDotL = max(dot(normal, lightDirection), 0.0);
        float vDotH = max(dot(viewDirection, halfDirection), 0.0);
        float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
        float distribution = a2 / max(3.14159265 * denominator * denominator, 1.0e-6);
        float k = (filteredRoughness + 1.0) * (filteredRoughness + 1.0) * 0.125;
        float visibility = (nDotV / max(nDotV * (1.0-k) + k, 1.0e-5))
            * (nDotL / max(nDotL * (1.0-k) + k, 1.0e-5));
        float solarFresnel = 0.02037 + 0.97963 * pow(1.0-vDotH, 5.0);
        directSpecular = uWaterKeyIrradiance * distribution * visibility * solarFresnel
            / max(4.0 * nDotV, 1.0e-4) * waterShadow * cloudVisibility * uSunGlintStrength;
    }
    vec3 color = mix(transmission, reflection, reflectance) + directSpecular;
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
