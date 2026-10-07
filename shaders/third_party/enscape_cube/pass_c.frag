#version 330 core
uniform bool uNoiseReduction;
uniform vec3 iResolution;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
out vec4 fragColor;
#include "buffer_c.glsl"
void main() { mainImage(fragColor, gl_FragCoord.xy); }
