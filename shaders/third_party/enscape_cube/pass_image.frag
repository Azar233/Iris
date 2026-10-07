#version 330 core
uniform bool uNoiseReduction;
uniform vec3 iResolution;
uniform sampler2D iChannel0;
out vec4 fragColor;
#include "image.glsl"
void main() { mainImage(fragColor, gl_FragCoord.xy); }
