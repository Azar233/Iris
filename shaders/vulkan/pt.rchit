#version 460
#extension GL_EXT_ray_tracing : require
struct Payload { ivec4 ids; vec4 data; };
layout(location=0) rayPayloadInEXT Payload hit;
hitAttributeEXT vec2 barycentric;
void main(){hit.ids=ivec4(gl_InstanceCustomIndexEXT,gl_PrimitiveID,gl_HitKindEXT==gl_HitKindFrontFacingTriangleEXT?1:0,0);hit.data=vec4(barycentric,gl_HitTEXT,0);}
