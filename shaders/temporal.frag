#version 450
layout(set=0,binding=0) uniform sampler2D sceneColor;
layout(set=0,binding=1) uniform sampler2D motionDepth;
layout(set=0,binding=2) uniform sampler2D historyColor;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resolved;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 extent = textureSize(sceneColor,0);
    vec3 current = texelFetch(sceneColor,pixel,0).rgb;
    vec4 motion = texelFetch(motionDepth,pixel,0);
    resolved = vec4(current,motion.w);
    vec2 previousUv = uv-motion.xy;
    if (motion.z <= 0.0 || any(lessThan(previousUv,vec2(0))) ||
        any(greaterThanEqual(previousUv,vec2(1)))) return;

    ivec2 previousPixel = ivec2(previousUv*vec2(extent));
    vec4 previous = texelFetch(historyColor,previousPixel,0);
    float tolerance = max(0.025,0.015*motion.z);
    if (abs(previous.a-motion.z) > tolerance) return;
    vec4 filtered = texture(historyColor,previousUv);
    vec3 history = abs(filtered.a-motion.z) <= tolerance ? filtered.rgb : previous.rgb;
    const ivec2 neighbors[] = ivec2[](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
    vec3 lo = current, hi = current;
    for (int i=0; i<neighbors.length(); ++i) {
        vec3 c = texelFetch(sceneColor,clamp(pixel+neighbors[i],ivec2(0),extent-1),0).rgb;
        lo = min(lo,c);
        hi = max(hi,c);
    }
    float speed = length(motion.xy*vec2(extent));
    float weight = mix(0.88,0.30,smoothstep(1.0,24.0,speed));
    resolved.rgb = mix(current,clamp(history,lo,hi),weight);
}
