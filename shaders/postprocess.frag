#version 330 core
#include "water_medium.glsl"
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uMotion;
uniform sampler2D uDepth;
uniform sampler2D uEncodedNormal;
uniform sampler3D uColorGradingLut;
uniform sampler2D uCloud;
uniform sampler2D uCloudDepth;
uniform sampler2D uGodRays;
uniform bool uGodRaysEnabled;
uniform vec3 uGodRaysColor;
uniform int uTemporalDebugView;
uniform bool uToneMapping;
uniform bool uBloomEnabled;
uniform bool uEncodeSrgb;
uniform float uExposure;
uniform float uBloomIntensity;
uniform bool uOutlineEnabled;
uniform bool uOutlineNormalAvailable;
uniform float uOutlineWidth;
uniform float uOutlineDepthThreshold;
uniform float uOutlineNormalThreshold;
uniform vec3 uOutlineColor;
uniform bool uDitherEnabled;
uniform float uDitherStrength;
uniform bool uHeightFogEnabled;
uniform float uHeightFogDensity;
uniform float uHeightFogBaseHeight;
uniform float uHeightFogFalloff;
uniform vec3 uHeightFogColor;
uniform bool uAerialEnabled;
uniform float uAerialStrength;
uniform float uAerialScaleHeight;
uniform vec3 uAerialColumnDepth;
uniform vec3 uAerialZenithColor;
uniform vec3 uAerialHorizonColor;
uniform bool uUnderwaterFogEnabled;
uniform bool uWaterSurfaceOptics;
uniform float uWaterSurfaceHeight;
uniform sampler2D uOpaqueDepth;
uniform vec3 uUnderwaterAbsorption;
uniform vec3 uUnderwaterColor;
uniform bool uColorGradingEnabled;
uniform bool uCloudEnabled;
uniform float uColorGradingStrength;
uniform int uStylizedDebugView;
uniform float uInverseWidth;
uniform float uInverseHeight;
uniform mat4 uInverseProjection;
uniform mat4 uInverseCurrentViewProjection;
uniform vec3 uCameraPosition;
out vec4 fragmentColor;

vec3 aces(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 color) {
    color = max(color, vec3(0.0));
    bvec3 cutoff = lessThanEqual(color, vec3(0.0031308));
    return mix(1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, color * 12.92, cutoff);
}

float orderedDitherThreshold() {
    const float pattern[16] = float[16](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0
    );
    ivec2 pixel = ivec2(mod(floor(gl_FragCoord.xy), 4.0));
    return (pattern[pixel.y * 4 + pixel.x] + 0.5) / 16.0;
}

vec3 applyOrderedDither(vec3 color) {
    const float colorLevels = 15.0;
    vec3 quantized = floor(
        clamp(color, 0.0, 1.0) * colorLevels + orderedDitherThreshold()
    ) / colorLevels;
    return mix(color, quantized, clamp(uDitherStrength, 0.0, 1.0));
}

// Reconstruction of the camera-to-surface segment. Sky has no finite surface depth in the current
// contract, so it reports the far flag; transparent surfaces reuse the opaque depth behind them and
// therefore fade with the geometry behind them instead of being skipped, which is the same
// compromise the height fog makes.
void worldSegment(float deviceDepth, out float distanceToSurface, out vec3 segment, out bool isSky) {
    distanceToSurface = 0.0;
    segment = vec3(0.0);
    isSky = deviceDepth >= 0.999999;
    if (isSky) return;
    vec4 world = uInverseCurrentViewProjection
        * vec4(vUv * 2.0 - 1.0, deviceDepth * 2.0 - 1.0, 1.0);
    vec3 worldPosition = world.xyz / max(abs(world.w), 1.0e-6);
    segment = worldPosition - uCameraPosition;
    distanceToSurface = length(segment);
}

float heightFogFactor(float deviceDepth, vec3 segment, float distanceToSurface, bool isSky) {
    if (isSky || uHeightFogDensity <= 0.0) return 0.0;
    // The segment already is `worldPosition - uCameraPosition`, so its Y is the height difference.
    float heightDelta = segment.y;
    float falloff = max(uHeightFogFalloff, 0.01);
    float cameraDensity = uHeightFogDensity * exp(clamp(
        -falloff * (uCameraPosition.y - uHeightFogBaseHeight), -20.0, 20.0
    ));
    float integratedDensity = abs(heightDelta) < 1.0e-4
        ? cameraDensity * distanceToSurface
        : cameraDensity * distanceToSurface
            * (1.0 - exp(clamp(-falloff * heightDelta, -20.0, 20.0)))
            / (falloff * heightDelta);
    return clamp(1.0 - exp(-max(integratedDensity, 0.0)), 0.0, 1.0);
}

float viewDepth(vec2 uv, float deviceDepth) {
    if (deviceDepth >= 0.999999) return 1.0e20;
    vec4 viewPosition = uInverseProjection
        * vec4(uv * 2.0 - 1.0, deviceDepth * 2.0 - 1.0, 1.0);
    return abs(viewPosition.z / max(abs(viewPosition.w), 1.0e-6));
}

// Aerial perspective. `uAerialColumnDepth` is the optical depth of the whole vertical column, so
// the segment in front of a surface is that column scaled by how much atmosphere the ray actually
// crosses: `airMass` for the angle, `length / scaleHeight` for the distance, and the vertical
// profile along the way. A horizontal ray walks one column's worth of air per scale height
// travelled, and a ray pointing up saturates on the full column no matter how long it is, which is
// `clamp(abs(direction.y), ...)` cancelling the `1 / abs(direction.y)` in the air mass. The
// in-scatter is the sky itself, interpolated from the zenith towards the horizon by view elevation,
// and is deliberately the disk-free sky: a 4.6e3 sun disk must not be smeared over the landscape.
vec3 aerialPerspectiveColor(vec3 color, float deviceDepth, float distanceToSurface, vec3 segment) {
    if (!uAerialEnabled || uAerialStrength <= 0.0 || deviceDepth >= 0.999999) return color;
    vec3 direction = segment / max(distanceToSurface, 1.0e-6);
    float vertical = abs(direction.y);
    float zenithDegrees = degrees(acos(clamp(vertical, 0.0, 1.0)));
    float horizonTerm = pow(max(96.07995 - zenithDegrees, 0.01), -1.6364);
    float airMass = 1.0 / max(cos(radians(zenithDegrees)) + 0.50572 * horizonTerm, 1.0e-4);
    float columnFraction = uAerialStrength * airMass * clamp(vertical, 1.0e-4, 1.0)
        * distanceToSurface / uAerialScaleHeight;
    vec3 transmittance = exp(-uAerialColumnDepth * columnFraction);
    // The horizon colour already carries the low-sun glow, so lifting it as the view flattens keeps
    // a sunset reading warm at eye level instead of fading everything into a flat grey.
    vec3 sky = mix(uAerialZenithColor, uAerialHorizonColor, pow(1.0 - vertical, 3.0));
    return mix(color, sky, clamp(1.0 - transmittance, vec3(0.0), vec3(1.0)));
}

float outlineEdge() {
    float centerDeviceDepth = texture(uDepth, vUv).r;
    // Draw silhouettes inward on opaque geometry. This avoids a halo over the
    // sky and deliberately leaves non-depth-writing transparent surfaces alone.
    if (centerDeviceDepth >= 0.999999) return 0.0;

    float centerDepth = viewDepth(vUv, centerDeviceDepth);
    vec3 centerNormal = vec3(0.0, 0.0, 1.0);
    if (uOutlineNormalAvailable) {
        centerNormal = normalize(texture(uEncodedNormal, vUv).xyz * 2.0 - 1.0);
    }
    vec2 texel = vec2(uInverseWidth, uInverseHeight) * max(uOutlineWidth, 0.5);
    vec2 offsets[8] = vec2[8](
        vec2(-1.0,  0.0), vec2(1.0,  0.0),
        vec2( 0.0, -1.0), vec2(0.0,  1.0),
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0,  1.0), vec2(1.0,  1.0)
    );
    float maximumDepthDelta = 0.0;
    float maximumNormalDelta = 0.0;
    for (int index = 0; index < 8; ++index) {
        vec2 sampleUv = clamp(vUv + offsets[index] * texel, vec2(0.0), vec2(1.0));
        float sampleDeviceDepth = texture(uDepth, sampleUv).r;
        if (sampleDeviceDepth >= 0.999999) {
            maximumDepthDelta = 1.0;
            continue;
        }
        float sampleDepth = viewDepth(sampleUv, sampleDeviceDepth);
        float relativeDelta = abs(sampleDepth - centerDepth)
            / max(min(sampleDepth, centerDepth), 0.25);
        maximumDepthDelta = max(maximumDepthDelta, relativeDelta);
        if (uOutlineNormalAvailable) {
            vec3 sampleNormal = normalize(
                texture(uEncodedNormal, sampleUv).xyz * 2.0 - 1.0
            );
            maximumNormalDelta = max(
                maximumNormalDelta,
                1.0 - clamp(dot(centerNormal, sampleNormal), -1.0, 1.0)
            );
        }
    }
    float depthEdge = smoothstep(
        uOutlineDepthThreshold,
        uOutlineDepthThreshold * 2.0,
        maximumDepthDelta
    );
    float normalEdge = uOutlineNormalAvailable
        ? smoothstep(
            uOutlineNormalThreshold,
            uOutlineNormalThreshold * 1.5,
            maximumNormalDelta
        )
        : 0.0;
    return max(depthEdge, normalEdge);
}

void main() {
    if (uTemporalDebugView == 1) {
        vec2 motion = texture(uMotion, vUv).rg;
        fragmentColor = vec4(clamp(vec3(0.5 + motion.x * 24.0, 0.5 + motion.y * 24.0, 0.5), 0.0, 1.0), 1.0);
        return;
    }
    if (uTemporalDebugView == 2) {
        float historyWeight = texture(uScene, vUv).a;
        fragmentColor = vec4(vec3(historyWeight), 1.0);
        return;
    }
    if (uStylizedDebugView == 1 || uStylizedDebugView == 2) {
        fragmentColor = vec4(clamp(texture(uScene, vUv).rgb, 0.0, 1.0), 1.0);
        return;
    }
    if (uStylizedDebugView == 3) {
        fragmentColor = vec4(vec3(outlineEdge()), 1.0);
        return;
    }
    vec3 color = texture(uScene, vUv).rgb;
    if (uBloomEnabled) color += texture(uBloom, vUv).rgb * uBloomIntensity;
    float deviceDepth = texture(uDepth, vUv).r;
    // The cloud layer composites here: after the opaque scene and bloom exist in one linear HDR
    // colour, and before tone mapping, so the layer's own HDR range survives. The attachment carries
    // `(scattered radiance, transmittance)`, so this is a radiance-over-background blend and the
    // background is whatever the scene had -- geometry the layer occludes, or the sky behind it.
    //
    // The height fog, the aerial perspective and the underwater fog below then treat the cloud as
    // part of the scene, which is what keeps one air model in front of everything rather than a
    // separate one for the layer.
    bool cloudPresent = false;
    if (uCloudEnabled) {
        // Depth-guided four-tap reconstruction from the cloud's own first-density depth. Opaque
        // depth only decides whether geometry is in front; it never guides the cloud upsample.
        ivec2 cloudSize = textureSize(uCloud, 0);
        vec2 location = vUv * vec2(cloudSize) - 0.5;
        ivec2 base = ivec2(floor(location));
        vec2 fraction = fract(location);
        ivec2 nearest = clamp(ivec2(floor(location + 0.5)), ivec2(0), cloudSize - 1);
        float referenceDepth = texelFetch(uCloudDepth, nearest, 0).r;
        vec4 layer = vec4(0.0);
        float sum = 0.0;
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                ivec2 samplePixel = clamp(base + ivec2(x,y), ivec2(0), cloudSize - 1);
                float depth = texelFetch(uCloudDepth, samplePixel, 0).r;
                float weight = (x == 0 ? 1.0 - fraction.x : fraction.x)
                    * (y == 0 ? 1.0 - fraction.y : fraction.y);
                if ((depth > 0.0) != (referenceDepth > 0.0)) weight = 0.0;
                else if (referenceDepth > 0.0)
                    weight *= exp(-abs(depth - referenceDepth) / max(referenceDepth * 0.1, 50.0));
                layer += texelFetch(uCloud, samplePixel, 0) * weight;
                sum += weight;
            }
        }
        layer = sum > 0.00001 ? layer / sum : texelFetch(uCloud, nearest, 0);
        if (deviceDepth < 0.999999 && referenceDepth > 0.0) {
            vec4 surface = uInverseCurrentViewProjection * vec4(vUv * 2.0 - 1.0, deviceDepth * 2.0 - 1.0, 1.0);
            float surfaceDistance = length(surface.xyz / surface.w - uCameraPosition);
            if (surfaceDistance < referenceDepth) layer = vec4(0.0, 0.0, 0.0, 1.0);
        }
        // A fully transmissive, non-scattering sample means the ray missed the slab entirely, and
        // skipping it keeps a cloudless frame bit-identical to what it was before this pass existed.
        cloudPresent = layer.a < 0.99999 || dot(layer.rgb, layer.rgb) > 0.0;
        color = layer.rgb + color * layer.a;
    }
    if (uGodRaysEnabled && deviceDepth >= 0.999999)
        color += texture(uGodRays, vUv).r * uGodRaysColor;
    // One reconstruction serves both effects: the height fog needs it only when it is on, but the
    // aerial perspective needs it whenever it is on, and neither can share the other's early-out.
    float aerialDistance = 0.0;
    vec3 aerialSegment = vec3(0.0);
    bool aerialIsSky = true;
    if (uAerialEnabled || uHeightFogEnabled || uUnderwaterFogEnabled) {
        worldSegment(deviceDepth, aerialDistance, aerialSegment, aerialIsSky);
    }
    float fogFactor = heightFogFactor(
        deviceDepth, aerialSegment, aerialDistance,
        uAerialEnabled || uHeightFogEnabled ? aerialIsSky : true
    );
    if (uStylizedDebugView == 5) {
        fragmentColor = vec4(vec3(fogFactor), 1.0);
        return;
    }
    if (uHeightFogEnabled) {
        color = mix(color, max(uHeightFogColor, vec3(0.0)), fogFactor);
    }
    // The air sits in front of the stylized fog, so it composites on top: the fog is a look, the
    // aerial perspective is the atmosphere the scene is standing in.
    color = aerialPerspectiveColor(color, deviceDepth, aerialDistance, aerialSegment);
    if (uUnderwaterFogEnabled) {
        float waterDistance = aerialIsSky ? 24.0 : min(aerialDistance, 24.0);
        if (uWaterSurfaceOptics) {
            bool interfaceHit = !aerialIsSky && deviceDepth < texture(uOpaqueDepth, vUv).r - 1.0e-6;
            vec4 farPoint = uInverseCurrentViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
            vec3 ray = aerialIsSky ? normalize(farPoint.xyz / farPoint.w - uCameraPosition)
                : aerialSegment / max(aerialDistance, 1.0e-6);
            waterDistance = waterMediumDistance(waterDistance, interfaceHit,
                uCameraPosition.y, uWaterSurfaceHeight, ray.y);
        }
        vec3 transmittance = exp(-max(uUnderwaterAbsorption, vec3(0.0))
            * waterDistance);
        color = color * transmittance
            + uUnderwaterColor * (vec3(1.0) - transmittance);
    }
    color *= max(uExposure, 0.0);
    color = uToneMapping ? aces(color) : clamp(color, 0.0, 1.0);
    color = uEncodeSrgb ? linearToSrgb(color) : color;
    vec3 lutCoordinate = (clamp(color, 0.0, 1.0) * 31.0 + 0.5) / 32.0;
    vec3 gradedColor = texture(uColorGradingLut, lutCoordinate).rgb;
    if (uStylizedDebugView == 6) {
        fragmentColor = vec4(clamp(abs(gradedColor - color) * 4.0, 0.0, 1.0), 1.0);
        return;
    }
    if (uColorGradingEnabled) {
        color = mix(color, gradedColor, clamp(uColorGradingStrength, 0.0, 1.0));
    }
    if (uStylizedDebugView == 4) {
        fragmentColor = vec4(vec3(orderedDitherThreshold()), 1.0);
        return;
    }
    if (uDitherEnabled) color = applyOrderedDither(color);
    if (uOutlineEnabled) {
        color = mix(color, max(uOutlineColor, vec3(0.0)), outlineEdge());
    }
    fragmentColor = vec4(color, 1.0);
}
