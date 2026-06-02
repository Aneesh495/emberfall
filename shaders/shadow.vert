#version 450
layout(location=0) in vec3 inPosition;
layout(location=2) in mat4 instanceModel;
layout(push_constant) uniform LightCamera { mat4 projection; } camera;

void main() {
    gl_Position = camera.projection * instanceModel * vec4(inPosition,1.0);
}
