// Explicit trilinear texelFetch mirrors NoiseVolume::sample. Hardware GL_LINEAR
// precision is not used for the reference contract. Inputs are noise-tile units.
uniform sampler3D uCloudNoiseVolume;
uniform float uCloudNoisePeriod;
int cloudNoiseWrap(int value, int size) { return (value + size) % size; }
vec4 cloudNoiseVoxel(ivec3 p, int size) {
    return texelFetch(uCloudNoiseVolume, ivec3(cloudNoiseWrap(p.x,size),
        cloudNoiseWrap(p.y,size),cloudNoiseWrap(p.z,size)), 0);
}
vec4 cloudNoiseMix(vec4 a, vec4 b, float t) { return a + (b - a) * t; }
vec4 sampleCloudNoise(vec3 position) {
    int size = textureSize(uCloudNoiseVolume, 0).x;
    vec3 p = fract(position / uCloudNoisePeriod) * float(size) - 0.5;
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    return cloudNoiseMix(
        cloudNoiseMix(cloudNoiseMix(cloudNoiseVoxel(i,size),cloudNoiseVoxel(i+ivec3(1,0,0),size),f.x),
            cloudNoiseMix(cloudNoiseVoxel(i+ivec3(0,1,0),size),cloudNoiseVoxel(i+ivec3(1,1,0),size),f.x),f.y),
        cloudNoiseMix(cloudNoiseMix(cloudNoiseVoxel(i+ivec3(0,0,1),size),cloudNoiseVoxel(i+ivec3(1,0,1),size),f.x),
            cloudNoiseMix(cloudNoiseVoxel(i+ivec3(0,1,1),size),cloudNoiseVoxel(i+ivec3(1,1,1),size),f.x),f.y),f.z);
}

float myrenderer_cloud_offline_sample(float x,float y,float z,int channel) {
    return sampleCloudNoise(vec3(x,y,z))[channel];
}

// Endpoint samples, Q24 decoded to R32F, explicit interpolation shared with CPU.
uniform sampler2D uCloudLightingLut;
float cloudTransmission(float depth, bool offline) {
    if(!offline || depth > 32.0) return exp(-depth);
    float x=max(depth,0.0)*256.0;
    int i=min(int(x),8191);
    float a=texelFetch(uCloudLightingLut,ivec2(i%129,i/129),0).r;
    int j=i+1;
    float b=texelFetch(uCloudLightingLut,ivec2(j%129,j/129),0).r;
    return a+(b-a)*(x-float(i));
}
