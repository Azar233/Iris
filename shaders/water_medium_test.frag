#version 330 core
#include "water_medium.glsl"
uniform vec4 uInterval; // receiver, eye height, surface height, ray Y
uniform bool uInterfaceHit;
out vec4 fragmentColor;
void main() {
    fragmentColor = vec4(waterMediumDistance(uInterval.x, uInterfaceHit,
        uInterval.y, uInterval.z, uInterval.w), 0.0, 0.0, 1.0);
}
