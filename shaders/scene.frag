#version 450
layout(location=0) in vec3 normal;
layout(location=1) in vec3 worldPosition;
layout(location=2) in vec4 tint;
layout(location=3) flat in vec4 surfaceData;
layout(location=4) flat in float metallic;
layout(location=5) in vec4 previousClip;
layout(location=6) flat in float historyValid;
layout(location=0) out vec4 hdr;
layout(location=1) out vec4 motion;

layout(push_constant) uniform SceneCamera {
    mat4 viewProjection;
    vec4 eye;
} camera;

struct LocalLight {
    vec4 positionRadius;
    vec4 colorPower;
};
layout(std140,set=0,binding=0) uniform Lighting {
    mat4 previousCamera;
    mat4 shadowProjection[9];
    vec4 cameraForward;
    vec4 cascadeEnds;
    vec4 directionCount;
    uvec4 tileGrid;
    uvec4 shadowOptions;
    vec4 viewport;
    LocalLight lamps[128];
} lighting;
layout(set=0,binding=1) uniform sampler2D shadowDepth;
layout(std430,set=0,binding=2) readonly buffer LightTiles {
    uvec4 masks[];
} tiles;

float sampleShadow(int viewIndex,vec3 position,float bias) {
    vec4 clip = lighting.shadowProjection[viewIndex]*vec4(position,1.0);
    if (clip.w<=0.0) return 1.0;
    vec3 coord = clip.xyz/clip.w;
    vec2 uv = coord.xy*0.5+0.5;
    if (any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0))) ||
        coord.z<=0.0 || coord.z>=1.0) return 1.0;
    int columns = int(lighting.shadowOptions.x);
    float side = float(lighting.shadowOptions.y);
    vec2 origin = vec2(viewIndex%columns,viewIndex/columns)*side;
    vec2 texel = 1.0/vec2(textureSize(shadowDepth,0));
    float sum = 0.0;
    for (int y=-1; y<=1; ++y) for (int x=-1; x<=1; ++x) {
        vec2 pixel = clamp(uv*side+vec2(x,y),vec2(0.5),vec2(side-0.5));
        float stored = texture(shadowDepth,(origin+pixel)*texel).r;
        sum += coord.z-bias <= stored ? 1.0 : 0.0;
    }
    return sum/9.0;
}

float sunVisibility(vec3 position,float normalLight) {
    float z = dot(position-camera.eye.xyz,lighting.cameraForward.xyz);
    int index = z<lighting.cascadeEnds.x ? 0 : z<lighting.cascadeEnds.y ? 1 : 2;
    float first = index==0 ? lighting.cameraForward.w : lighting.cascadeEnds[index-1];
    float end = lighting.cascadeEnds[index];
    float fade = smoothstep(end-(end-first)*0.085,end,z);
    float bias = max(0.00028,0.00078*(1.0-normalLight));
    float current = sampleShadow(index,position,bias);
    if (fade<=0.0) return current;
    float next = index==2 ? 1.0 : sampleShadow(index+1,position,bias);
    return mix(current,next,fade);
}

int pointFace(vec3 direction) {
    vec3 weight = abs(direction);
    if (weight.x>=weight.y && weight.x>=weight.z) return direction.x>=0.0 ? 0 : 1;
    if (weight.y>=weight.z) return direction.y>=0.0 ? 2 : 3;
    return direction.z>=0.0 ? 4 : 5;
}

vec3 materialLight(vec3 n, vec3 v, vec3 l, vec3 radiance) {
    float nl = max(dot(n,l),0.0);
    if (nl <= 0.0) return vec3(0);
    float nv = max(dot(n,v),0.001);
    vec3 h = normalize(l+v+n*0.00001);
    float nh = max(dot(n,h),0.0);
    float vh = clamp(dot(v,h),0.0,1.0);
    float alpha = max(0.045,surfaceData.w);
    alpha *= alpha;
    float alpha2 = alpha*alpha;
    float denominator = nh*nh*(alpha2-1.0)+1.0;
    const float pi = 3.14159265;
    float distribution = alpha2 / max(pi*denominator*denominator,0.00000001);
    float geometry = 0.5 / max(nl*sqrt(nv*nv*(1.0-alpha2)+alpha2) +
                               nv*sqrt(nl*nl*(1.0-alpha2)+alpha2),0.0001);
    vec3 f0 = mix(vec3(0.04),tint.rgb,metallic);
    vec3 fresnel = f0+(1.0-f0)*pow(1.0-vh,5.0);
    vec3 diffuse = (1.0-fresnel)*(1.0-metallic)*tint.rgb/pi;
    return (diffuse+fresnel*distribution*geometry)*radiance*nl;
}

void main() {
    vec3 n = normalize(normal);
    vec3 v = normalize(camera.eye.xyz - worldPosition);
    vec3 light = lighting.directionCount.xyz;
    vec3 halfVector = normalize(v + light);
    float lambert = max(dot(n, light), 0.0);
    float grazing = pow(clamp(1.0 - max(dot(n, v), 0.0), 0.0, 1.0), 3.0);
    float highlight = 0.17 * pow(max(dot(n, halfVector), 0.0), 28.0) * lambert;
    vec3 ambient = mix(vec3(0.09, 0.13, 0.23), vec3(0.23, 0.30, 0.42), n.y*0.5+0.5);
    vec3 warm = vec3(1.17, 0.88, 0.67) * lambert;
    vec2 tile = abs(fract(worldPosition.xz + 0.5) - 0.5);
    float grid = 1.0 - smoothstep(0.01, 0.04, min(tile.x,tile.y));
    float surface = 1.0 - grid*max(n.y, 0.0)*0.16;
    float sun = lambert > 0.0 ? sunVisibility(worldPosition,lambert) : 0.0;
    vec3 result = tint.rgb*(ambient+warm*sun)*surface + highlight*sun + grazing*vec3(0.03,0.065,0.10);
    bool material = surfaceData.w >= 0.0;
    if (material) {
        vec3 reflected = reflect(-v,n);
        vec3 sky = mix(vec3(0.05,0.07,0.10),vec3(0.20,0.30,0.42),reflected.y*0.5+0.5);
        result = tint.rgb*(ambient*(1.0-metallic)+sky*metallic) + surfaceData.rgb;
        result += materialLight(n,v,light,vec3(2.8,2.2,1.7))*sun;
    }
    uvec2 tileCoord = uvec2(gl_FragCoord.xy) / lighting.tileGrid.z;
    uvec4 mask = tiles.masks[tileCoord.y*lighting.tileGrid.x + tileCoord.x];
    for (int word=0; word<4; ++word) {
        uint bits = mask[word];
        while (bits != 0u) {
            int bit = findLSB(bits);
            bits &= bits-1u;
            int lightIndex = word*32 + bit;
            LocalLight lamp = lighting.lamps[lightIndex];
            vec3 offset = lamp.positionRadius.xyz-worldPosition;
            float distance2 = dot(offset,offset);
            float radius2 = lamp.positionRadius.w*lamp.positionRadius.w;
            if (distance2 >= radius2) continue;
            float cone = 1.0-distance2/radius2;
            float facing = max(dot(n,offset*inversesqrt(max(distance2,0.0001))),0.0);
            vec3 radiance = lamp.colorPower.rgb*(lamp.colorPower.w*cone*cone/(1.0+distance2));
            if (uint(lightIndex)==lighting.shadowOptions.z && facing>0.0) {
                radiance *= sampleShadow(3+pointFace(-offset),worldPosition,0.0014);
            }
            if (material) result += materialLight(n,v,offset*inversesqrt(max(distance2,0.0001)),radiance);
            else result += tint.rgb*radiance*facing;
        }
    }
    result += tint.rgb*tint.a;
    float fog = 1.0-exp(-length(camera.eye.xyz-worldPosition)*0.021);
    hdr = vec4(mix(result,vec3(0.042,0.067,0.11),fog*0.72),1.0);
    motion = vec4(0.0,0.0,0.0,1.0/gl_FragCoord.w);
    if (historyValid > 0.0 && previousClip.w > 0.1) {
        vec2 previousUv = previousClip.xy/previousClip.w*0.5+0.5;
        motion.xyz = vec3(gl_FragCoord.xy*lighting.viewport.xy-previousUv,previousClip.w);
    }
}
