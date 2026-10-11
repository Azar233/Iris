float hash( float n )
{
    return fract(sin(n)*43758.5453);
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 tonemapACES( vec3 x )
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return (x*(a*x+b))/(x*(c*x+d)+e);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 q = fragCoord.xy / iResolution.xy;
    vec2   blurRadius    = vec2(20.0) / iResolution.xy;

    vec4 sum = vec4(0.0);
    float NUM_SAMPLES = 20.;
    float phiOffset = uNoiseReduction ? 0.5 : hash(dot(fragCoord.xy, vec2(1.12,2.251)) + iTime);
    for(float i = 0.; i < NUM_SAMPLES; i++)
    {
        vec2 r = blurRadius * i / NUM_SAMPLES;
        float phi = (i / NUM_SAMPLES + phiOffset) * 2.0 * 3.1415926;
        vec2 uv = q + vec2(sin(phi), cos(phi))*r;
        sum += textureLod(iChannel0, uv, 0.0);
    }
    float BLOOM_AMOUNT = 0.05 * uBloomStrength;
    vec4 center = textureLod(iChannel0, q, 0.0);
    vec3 surface = center.rgb;
    if (uNoiseReduction && center.a > 0.5) {
        // Pixel integration only on water. Never mix opaque/sky neighbours into
        // this footprint; retain the original whole-scene bloom separately.
        vec2 texel = 1.0 / iResolution.xy;
        vec3 integrated = vec3(0.0);
        float totalWeight = 0.0;
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            vec4 sampleValue = textureLod(iChannel0, q + vec2(x,y) * texel, 0.0);
            float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            if (sampleValue.a > 0.5) {
                integrated += sampleValue.rgb * weight;
                totalWeight += weight;
            }
        }
        surface = integrated / max(totalWeight, 1.0);
    }
    sum.xyz = mix(surface, sum.xyz / NUM_SAMPLES, BLOOM_AMOUNT);
    // Make it look as if some auto exposure magic is going on
    float exposure = 0.06 * uExposure * (1.0+0.2*sin(0.5*iTime)*sin(1.8*iTime));
    fragColor = vec4(tonemapACES(exposure*sum.xyz), 1.0);
}
