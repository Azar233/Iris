#version 330 core
uniform vec3 iResolution;
uniform float iTime;
uniform vec4 iMouse;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler3D iChannel2;
uniform sampler2D iChannel3;
out vec4 fragColor;
#include "buffer_a.glsl"
void main() { mainImage(fragColor, gl_FragCoord.xy); }
