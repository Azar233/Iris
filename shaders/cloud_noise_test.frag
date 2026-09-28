#version 330 core
#include "cloud_noise_sample.glsl"
layout(location=0) out vec4 outNoise;
uniform int uWidth;
uniform int uHeight;
vec3 testPosition(ivec2 pixel) {
    if (pixel.y == 0 && pixel.x < 4) {
        float value = pixel.x == 0 ? 0.0 : (pixel.x == 1 ? uCloudNoisePeriod
            : (pixel.x == 2 ? -uCloudNoisePeriod : uCloudNoisePeriod / float(textureSize(uCloudNoiseVolume,0).x) * 0.5));
        return vec3(value);
    }
    vec2 uv = (vec2(pixel)+0.5) / vec2(uWidth,uHeight);
    return vec3((-1.5 + uv.x * 4.0) * uCloudNoisePeriod,
        (-0.75 + uv.y * 2.0) * uCloudNoisePeriod,
        (-0.5 + uv.x * 0.75 + uv.y * 1.25) * uCloudNoisePeriod);
}
uniform bool uTestLighting;
void main() {
    if(uTestLighting){float depth=float(int(gl_FragCoord.y)*uWidth+int(gl_FragCoord.x))*40.0/float(uWidth*uHeight-1);outNoise=vec4(cloudTransmission(depth,true),exp(-depth),depth,1);}
    else outNoise = sampleCloudNoise(testPosition(ivec2(gl_FragCoord.xy)));
}
