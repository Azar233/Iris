#version 330 core
#include "inputs.glsl"
uniform sampler2D irisChannel0;
uniform sampler2D irisChannel1;
#define iChannel0 irisChannel0
#define iChannel1 irisChannel1
vec4 packageHistory(vec2 uv) {
    return irisHistoryValid ? textureLod(irisChannel1, uv, 0.0) : textureLod(irisChannel0, uv, 0.0);
}
#include "buffer_c.glsl"
