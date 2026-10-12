#version 330 core
#include "sequence-common.glsl"
uniform sampler2D irisChannel0;
uniform vec2 irisResolution;
uniform float gain;
uniform int irisSeed;
out vec4 color;
void main() {
    color = texture(irisChannel0, gl_FragCoord.xy / irisResolution)
        + vec4(gain + sequenceSeedGain(irisSeed), 0, 0, 0);
}
