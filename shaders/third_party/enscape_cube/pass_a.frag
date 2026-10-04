#version 330 core
uniform vec3 iResolution;
uniform float iTime;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uCameraTanHalfFov;
uniform float uCameraNear;
uniform float uWaveHeight;
uniform float uWaveFrequency;
uniform float uWaveChoppiness;
uniform float uWaveSpeed;
uniform float uCloudCoverage;
uniform float uReflectionStrength;
uniform vec3 uSunDirection;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler3D iChannel2;
uniform sampler2D iChannel3;
out vec4 fragColor;
#include "buffer_a.glsl"
void main() { mainImage(fragColor, gl_FragCoord.xy); }
