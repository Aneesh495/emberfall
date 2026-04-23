#version 450
layout(location=0) in vec3 normal;
layout(location=1) in vec3 worldPosition;
layout(location=2) in vec4 tint;
layout(location=0) out vec4 hdr;

layout(push_constant) uniform SceneCamera {
    mat4 viewProjection;
    vec4 eye;
} camera;

void main() {
    vec3 n = normalize(normal);
    vec3 v = normalize(camera.eye.xyz - worldPosition);
    vec3 light = normalize(vec3(-0.45, 0.82, 0.24));
    vec3 halfVector = normalize(v + light);
    float lambert = max(dot(n, light), 0.0);
    float grazing = pow(clamp(1.0 - max(dot(n, v), 0.0), 0.0, 1.0), 3.0);
    float highlight = 0.17 * pow(max(dot(n, halfVector), 0.0), 28.0) * lambert;
    vec3 ambient = mix(vec3(0.10, 0.16, 0.27), vec3(0.32, 0.38, 0.49), n.y*0.5+0.5);
    vec3 warm = vec3(1.22, 0.91, 0.66) * lambert;
    vec2 tile = abs(fract(worldPosition.xz + 0.5) - 0.5);
    float grid = 1.0 - smoothstep(0.01, 0.04, min(tile.x,tile.y));
    float surface = 1.0 - grid*max(n.y, 0.0)*0.16;
    vec3 result = tint.rgb*(ambient+warm)*surface + highlight + grazing*vec3(0.03,0.065,0.10);
    result += tint.rgb*tint.a;
    float fog = 1.0-exp(-length(camera.eye.xyz-worldPosition)*0.021);
    hdr = vec4(mix(result,vec3(0.042,0.067,0.11),fog*0.72),1.0);
}
