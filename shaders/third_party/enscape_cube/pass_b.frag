#version 330 core
uniform vec3 iResolution;
uniform float iTime;
uniform float uBloomStrength;
uniform float uExposure;
uniform sampler2D iChannel0;
out vec4 fragColor;
#include "buffer_b.glsl"
void main() { mainImage(fragColor, gl_FragCoord.xy); }
