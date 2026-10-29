#version 460
#extension GL_EXT_ray_tracing : require
struct Payload { ivec4 ids; vec4 data; };
layout(location=0) rayPayloadInEXT Payload hit;
void main(){hit.ids.x=-1;}
