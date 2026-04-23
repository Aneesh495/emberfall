#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in mat4 instanceModel;
layout(location=6) in vec4 instanceTint;

layout(location=0) out vec3 normal;
layout(location=1) out vec3 worldPosition;
layout(location=2) out vec4 tint;

layout(push_constant) uniform SceneCamera {
    mat4 viewProjection;
    vec4 eye;
} camera;

void main() {
    vec4 world = instanceModel * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    normal = transpose(inverse(mat3(instanceModel))) * inNormal;
    tint = instanceTint;
    gl_Position = camera.viewProjection * world;
}
