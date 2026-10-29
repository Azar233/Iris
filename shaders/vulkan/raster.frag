#version 460
#extension GL_GOOGLE_include_directive : require
#ifdef IRIS_RAY_QUERY
#extension GL_EXT_ray_query : require
layout(set=0,binding=5) uniform accelerationStructureEXT sceneAcceleration;
#endif
#include "frame.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D baseTexture;
layout(set=0,binding=2) uniform sampler2D mrTexture;
layout(set=0,binding=3) uniform sampler2D environmentTexture;
layout(set=0,binding=4) uniform sampler2D normalTexture;
const float PI=3.14159265359;
vec3 environment(vec3 d) {
    vec2 p=vec2(atan(d.z,d.x)/(2*PI)+0.5,acos(clamp(d.y,-1,1))/PI);
    return frame.environmentInfo.z>0.5 ? texture(environmentTexture,p).rgb*frame.environmentInfo.x : vec3(0);
}
vec3 display(vec3 c) {
    c=max(c,vec3(0))*frame.eyeExposure.w;
    if(frame.sunDirectionTone.w>0.5) c=clamp((c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14),0,1);
    return clamp(c,0,1); // The sRGB attachment encodes once.
}
vec3 fresnel(float cosine,vec3 f0) {return f0+(1-f0)*pow(1-clamp(cosine,0,1),5);}
vec3 brdf(vec3 n,vec3 v,vec3 l,vec3 base,float metal,float rough) {
    vec3 h=normalize(v+l);
    float nv=max(dot(n,v),0.001),nl=max(dot(n,l),0),nh=max(dot(n,h),0);
    float a=rough*rough,a2=a*a;
    float den=nh*nh*(a2-1)+1;
    float distribution=a2/max(PI*den*den,0.00001);
    float k=(rough+1)*(rough+1)/8;
    float g=nv/(nv*(1-k)+k)*nl/max(nl*(1-k)+k,0.00001);
    vec3 f=fresnel(dot(h,v),mix(vec3(0.04),base,metal));
    return ((1-f)*(1-metal)*base/PI+distribution*g*f/max(4*nv*nl,0.0001))*nl;
}
float sunVisibility() {
#ifdef IRIS_RAY_QUERY
    vec3 l=normalize(-frame.sunDirectionTone.xyz);
    vec3 geometric=normalize(cross(dFdx(position),dFdy(position)));
    if(dot(geometric,l)<0)geometric=-geometric;
    rayQueryEXT query;
    rayQueryInitializeEXT(query,sceneAcceleration,gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsOpaqueEXT,
        255,position+geometric*0.002,0.001,l,100000.0);
    while(rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT?1.0:0.0;
#else
    return 1.0;
#endif
}
void main() {
    if(material.parameters.w>0.5 && material.parameters.w<1.5) {
        vec3 bg=frame.background.rgb;
        if(frame.environmentInfo.y>0.5 && frame.environmentInfo.z>0.5) bg=environment(normalize(position-frame.eyeExposure.xyz));
        color=vec4(display(bg),1); return;
    }
    vec4 base=texture(baseTexture,uv)*material.base;
    if(base.a<material.parameters.y) discard;
    if(material.parameters.w>1.5 && !gl_FrontFacing && material.parameters.z<1.5) discard;
    vec3 n=normalize(normal);
    if(material.parameters.z>1.5 && !gl_FrontFacing) n=-n;
    if(mod(material.parameters.z,2)>0.5) {
        vec3 q1=dFdx(position),q2=dFdy(position);vec2 s1=dFdx(uv),s2=dFdy(uv);
        float det=s1.x*s2.y-s1.y*s2.x;
        if(abs(det)>1e-8) {
            vec3 t=normalize((q1*s2.y-q2*s1.y)/det);
            t=normalize(t-n*dot(n,t));vec3 b=normalize(cross(n,t));
            if(dot(b,(q2*s1.x-q1*s2.x)/det)<0)b=-b;
            n=normalize(mat3(t,b,n)*(texture(normalTexture,uv).xyz*2-1));
        }
    }
    vec3 v=normalize(frame.eyeExposure.xyz-position);
    vec3 mr=texture(mrTexture,uv).rgb;
    float metal=clamp(material.emissionMetallic.w*mr.b,0,1),rough=clamp(material.parameters.x*mr.g,0.045,1);
    float visibility=sunVisibility();
#ifdef IRIS_RAY_QUERY
    if(frame.sunRadiance.w>0.5){color=vec4(vec3(visibility),1);return;}
#endif
    vec3 c=material.emissionMetallic.rgb+brdf(n,v,normalize(-frame.sunDirectionTone.xyz),base.rgb,metal,rough)*frame.sunRadiance.rgb*visibility;
    for(int i=0;i<int(frame.environmentInfo.w);++i) {
        vec3 delta=frame.localPosition[i].xyz-position;float distance=length(delta);
        vec3 l=delta/max(distance,0.0001);float radius=frame.localDirection[i].w;
        float attenuation=pow(clamp(1-pow(distance/radius,4),0,1),2)/max(distance*distance,0.01);
        if(frame.localPosition[i].w>0.5) {
            float cosine=dot(-l,frame.localDirection[i].xyz),outer=frame.localRadiance[i].w;
            attenuation*=smoothstep(outer,mix(outer,1,0.25),cosine);
        }
        c+=brdf(n,v,l,base.rgb,metal,rough)*frame.localRadiance[i].rgb*attenuation;
    }
    // Deterministic low-sample diffuse irradiance and rough specular environment.
    vec3 tangent=normalize(cross(abs(n.y)<0.99?vec3(0,1,0):vec3(1,0,0),n));
    vec3 bitangent=cross(n,tangent),diffuse=vec3(0);
    for(int i=0;i<16;++i) {
        float r=sqrt((i+0.5)/16.0),phi=i*2.39996323;
        diffuse+=environment(tangent*(r*cos(phi))+bitangent*(r*sin(phi))+n*sqrt(1-r*r));
    }
    vec3 f=fresnel(max(dot(n,v),0),mix(vec3(0.04),base.rgb,metal));
    vec3 reflection=reflect(-v,n),specular=vec3(0);
    for(int i=0;i<8;++i) {
        float phi=i*2.39996323;
        vec3 d=normalize(reflection+rough*rough*(tangent*cos(phi)+bitangent*sin(phi)));
        specular+=environment(d);
    }
    c+=(1-f)*(1-metal)*base.rgb*(diffuse/16)+f*specular/8;
    color=vec4(display(c),1);
}
