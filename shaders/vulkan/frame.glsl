layout(set=0,binding=0,std140) uniform Frame {
    mat4 vp;
    mat4 inverseVP;
    vec4 eyeExposure;
    vec4 sunDirectionTone;
    vec4 sunRadiance;
    vec4 environmentInfo;
    vec4 background;
    vec4 localPosition[16];
    vec4 localDirection[16];
    vec4 localRadiance[16];
} frame;
layout(push_constant) uniform Material {
    vec4 base;
    vec4 emissionMetallic;
    vec4 parameters; // roughness, alpha cutoff, normal map enabled, background pass
} material;
