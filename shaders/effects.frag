#version 450
layout(location=0) in vec2 uv;
layout(location=1) flat in vec4 energy;
layout(location=2) flat in uint kind;
layout(location=0) out vec4 light;

void main() {
    float distance = length(uv);
    float coverage = kind == 1u ? 1.0-smoothstep(0.03,0.13,abs(distance-0.78)) :
                                   1.0-smoothstep(0.12,1.0,distance);
    light = vec4(energy.rgb*coverage*energy.a,0.0);
}
