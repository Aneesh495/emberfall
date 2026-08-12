#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in mat4 instanceModel;
layout(location=6) in vec4 instanceTint;
layout(location=7) in vec4 instanceSurface;
layout(location=8) in float instanceMetallic;
layout(location=9) in mat4 instancePrevious;
layout(location=13) in uvec4 instanceTextureData;
layout(location=14) in vec2 inUv;
layout(location=15) in vec4 inTangent;

layout(location=0) out vec3 normal;
layout(location=1) out vec3 worldPosition;
layout(location=2) out vec4 tint;
layout(location=3) flat out vec4 surfaceData;
layout(location=4) flat out float metallic;
layout(location=5) out vec4 previousClip;
layout(location=6) out vec2 uv;
layout(location=7) out vec4 tangent;
layout(location=8) flat out uvec4 textureData;
layout(std140,set=0,binding=0) uniform Lighting {
    mat4 previousCamera;
} lighting;

layout(push_constant) uniform SceneCamera {
    mat4 viewProjection;
    vec4 eye;
} camera;

void main() {
    vec4 world = instanceModel * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    normal = transpose(inverse(mat3(instanceModel))) * inNormal;
    tint = instanceTint;
    surfaceData = instanceSurface;
    metallic = instanceMetallic;
    gl_Position = camera.viewProjection * world;
    uv = inUv;
    textureData = instanceTextureData;
    tangent = vec4(0.0);
    if (textureData.z != 0u) {
        mat3 basis = mat3(instanceModel);
        tangent = vec4(basis*inTangent.xyz,inTangent.w*sign(determinant(basis)));
    }
    previousClip = instancePrevious[3].w > 0.0
        ? lighting.previousCamera * instancePrevious * vec4(inPosition,1.0) : vec4(0.0);
}
