#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=0) out vec3 position;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 uv;
void main() {
    position=inPosition; normal=inNormal; uv=inUV;
    gl_Position=frame.vp*vec4(position,1);
    gl_Position.y=-gl_Position.y;
    gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;
}
