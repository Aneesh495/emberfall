#version 450
layout(set=0,binding=0) uniform sampler2D sceneColor;
layout(set=0,binding=1) uniform sampler2D effectColor;
layout(push_constant) uniform Display {
    float exposure;
    int filterEdges;
    int encodeSrgb;
} display;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outputColor;

vec3 curve(vec3 color) {
    color *= display.exposure;
    return clamp((color*(2.51*color+0.03))/(color*(2.43*color+0.59)+0.14),0.0,1.0);
}

float luminance(vec3 color) {
    return dot(color,vec3(0.2126,0.7152,0.0722));
}

vec3 hdr(vec2 point) {
    return texture(sceneColor,point).rgb+texture(effectColor,point).rgb;
}

void main() {
    vec3 center = curve(hdr(uv));
    if (display.filterEdges != 0) {
        vec2 pixel = 1.0/vec2(textureSize(sceneColor,0));
        vec3 left = curve(hdr(uv-vec2(pixel.x,0)));
        vec3 right = curve(hdr(uv+vec2(pixel.x,0)));
        vec3 top = curve(hdr(uv-vec2(0,pixel.y)));
        vec3 bottom = curve(hdr(uv+vec2(0,pixel.y)));
        float x = abs(luminance(right)-luminance(left));
        float y = abs(luminance(bottom)-luminance(top));
        if (max(x,y)>0.14) {
            center = mix(center,x>y ? (left+right)*0.5 : (top+bottom)*0.5,0.30);
        }
    }
    if (display.encodeSrgb != 0) {
        center = mix(12.92*center, 1.055*pow(center,vec3(1.0/2.4))-0.055,
                     greaterThan(center,vec3(0.0031308)));
    }
    outputColor = vec4(center,1.0);
}
