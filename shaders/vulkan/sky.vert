#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location=0) out vec3 position;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 uv;
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2)*2.0-1.0;
    vec4 world=frame.inverseVP*vec4(p.x,-p.y,1,1);
    position=world.xyz/world.w;
    normal=vec3(0,1,0); uv=vec2(0);
    gl_Position=vec4(p,1,1);
}
