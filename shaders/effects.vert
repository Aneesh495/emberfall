#version 450
layout(push_constant) uniform Emitter {
    mat4 camera;
    vec4 positionAge;
    vec4 colorRadius;
    vec2 projectionScale;
    uint seed;
    uint kind;
} effect;
layout(location=0) out vec2 uv;
layout(location=1) flat out vec4 energy;
layout(location=2) flat out uint kind;

float randomUnit(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return float(x & 0xffffu)/65535.0;
}

void main() {
    const vec2 corners[6] = vec2[](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    uv = corners[gl_VertexIndex];
    kind = effect.kind;
    float seconds = effect.positionAge.w;
    vec3 center = effect.positionAge.xyz;
    float radius = effect.colorRadius.w;
    if (kind == 1u) {
        radius *= 0.55+seconds*1.5;
        center.xz += uv*radius;
        gl_Position = effect.camera*vec4(center,1.0);
        energy = vec4(effect.colorRadius.rgb,pow(max(0.0,1.0-seconds/1.5),1.6));
        return;
    }

    uint particle = effect.seed+uint(gl_InstanceIndex)*0x9e3779b9u;
    float angle = 6.2831853*randomUnit(particle);
    float speed = mix(0.24,1.55,randomUnit(particle+1u));
    float lift = mix(1.1,3.2,randomUnit(particle+2u));
    float life = mix(0.55,1.48,randomUnit(particle+3u));
    vec3 velocity = vec3(cos(angle)*speed,lift,sin(angle)*speed);
    center += velocity*seconds - vec3(0.0,2.65*seconds*seconds,0.0);
    gl_Position = effect.camera*vec4(center,1.0);
    float phase = clamp(seconds/life,0.0,1.0);
    gl_Position.xy += uv*effect.projectionScale*radius*(1.0-phase*0.75);
    float alpha = (1.0-phase)*(1.0-phase)*smoothstep(0.0,0.08,seconds);
    energy = vec4(effect.colorRadius.rgb,alpha);
}
