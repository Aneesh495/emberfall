#version 450
layout(location=0) in vec4 tint;
layout(location=0) out vec4 pixel;
layout(push_constant) uniform Overlay { int encodeSrgb; } display;

void main() {
    vec3 value = tint.rgb;
    if (display.encodeSrgb != 0) {
        value = mix(12.92*value,1.055*pow(value,vec3(1.0/2.4))-0.055,greaterThan(value,vec3(0.0031308)));
    }
    pixel = vec4(value,tint.a);
}
