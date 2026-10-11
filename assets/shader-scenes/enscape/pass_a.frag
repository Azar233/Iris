#version 330 core
#include "inputs.glsl"
uniform sampler2D irisChannel0;
uniform sampler3D irisChannel2;
#define iChannel0 irisChannel0
#define iChannel2 irisChannel2
#include "buffer_a.glsl"
