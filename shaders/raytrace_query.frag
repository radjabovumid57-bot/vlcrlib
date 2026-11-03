#version 460
#extension GL_EXT_ray_query : require

/* Hardware-accelerated Cornell-box ray query demo.
 * Geometry is traversed through a Vulkan TLAS; this is not analytic
 * screen-space intersection code. */
layout(set=0,binding=0) uniform accelerationStructureEXT topLevelAS;
layout(std430,set=0,binding=1) readonly buffer TriangleVertices { vec4 vertex[]; } vertices;
layout(std430,set=0,binding=2) readonly buffer TriangleMaterials { uint material[]; } materials;
layout(push_constant) uniform RayParams { float timeSeconds; float width; float height; } params;
layout(location=0) out vec4 outColor;

struct Hit { float t; vec3 position; vec3 normal; vec3 geometricNormal; uint material; bool found; };

Hit traceScene(vec3 origin, vec3 direction, float tMax) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query,topLevelAS,gl_RayFlagsOpaqueEXT,0xff,origin,0.001,direction,tMax);
    while (rayQueryProceedEXT(query)) { }
    Hit h; h.found=false; h.t=tMax; h.position=vec3(0); h.normal=vec3(0,1,0);
    h.geometricNormal=vec3(0,1,0); h.material=0;
    if (rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionTriangleEXT) {
        uint primitive=rayQueryGetIntersectionPrimitiveIndexEXT(query,true);
        uint base=primitive*3u;
        vec3 a=vertices.vertex[base+0u].xyz;
        vec3 b=vertices.vertex[base+1u].xyz;
        vec3 c=vertices.vertex[base+2u].xyz;
        h.t=rayQueryGetIntersectionTEXT(query,true);
        h.position=origin+direction*h.t;
        h.material=materials.material[primitive];
        h.geometricNormal=normalize(cross(b-a,c-a));
        if (dot(h.geometricNormal,direction)>0.0) h.geometricNormal=-h.geometricNormal;
        h.normal=h.material==4u
            ? normalize(h.position-vec3(0.72,-0.72,-1.45))
            : h.geometricNormal;
        if (dot(h.normal,direction)>0.0) h.normal=-h.normal;
        h.found=true;
    }
    return h;
}

vec3 albedo(uint m) {
    if (m==1u) return vec3(0.72,0.075,0.055);
    if (m==2u) return vec3(0.075,0.48,0.12);
    if (m==3u) return vec3(0.22,0.20,0.19);
    return vec3(0.73,0.72,0.68);
}

/* Classic Phong material coefficients (ka, kd, ks, shininess).
 * The glass sphere is shaded with traced Fresnel reflection/refraction below. */
void phongMaterial(uint m, out vec3 ka, out vec3 kd, out vec3 ks, out float shininess) {
    ka=vec3(0.055);
    kd=albedo(m);
    ks=vec3(0.035);
    shininess=20.0;
    if (m==3u) { ks=vec3(0.12); shininess=32.0; }
    else if (m==1u || m==2u) { ks=vec3(0.07); shininess=28.0; }
}

float shadowVisibility(vec3 p, vec3 n, vec3 geometricNormal, vec3 lightPoint, float distanceToLight) {
    vec3 direction=normalize(lightPoint-p);
    /* Bias along the triangle normal, not the interpolated sphere normal;
     * otherwise the shadow ray can start inside a curved triangle. */
    float side=dot(geometricNormal,direction)>=0.0 ? 1.0 : -1.0;
    vec3 origin=p+geometricNormal*(side*0.006);
    float remaining=distanceToLight-0.006;
    float visibility=1.0;
    /* A glass sphere attenuates light instead of casting an opaque black
     * silhouette. Advancing through both interfaces handles its two sides. */
    for (int layer=0;layer<4;layer++) {
        Hit h=traceScene(origin,direction,remaining);
        if (!h.found) return visibility;
        if (h.material!=4u) return 0.0;
        visibility*=0.72;
        float advance=h.t+0.004;
        origin+=direction*advance;
        remaining-=advance;
        if (remaining<=0.0) return visibility;
    }
    return visibility;
}

float radicalInverseVdC(uint bits) {
    bits=(bits<<16u)|(bits>>16u);
    bits=((bits&0x55555555u)<<1u)|((bits&0xaaaaaaaau)>>1u);
    bits=((bits&0x33333333u)<<2u)|((bits&0xccccccccu)>>2u);
    bits=((bits&0x0f0f0f0fu)<<4u)|((bits&0xf0f0f0f0u)>>4u);
    bits=((bits&0x00ff00ffu)<<8u)|((bits&0xff00ff00u)>>8u);
    return float(bits)*2.3283064365386963e-10;
}

vec3 phongLighting(vec3 p, vec3 n, vec3 geometricNormal, vec3 viewDirection, uint materialId) {
    vec3 ka, kd, ks;
    float shininess;
    phongMaterial(materialId,ka,kd,ks,shininess);
    vec3 ambient=ka*vec3(0.30,0.31,0.34);
    vec3 direct=vec3(0);
    /* 8x8 stratified area-light samples smooth the penumbra without
     * stochastic grain; samples cover the whole rectangular emitter. */
    const uint SAMPLE_COUNT=64u;
    for (uint i=0u;i<SAMPLE_COUNT;i++) {
        /* Hammersley low-discrepancy points avoid the visible 8x8 light-grid
         * pattern while remaining deterministic and free of temporal noise. */
        vec2 uv=vec2((float(i)+0.5)/float(SAMPLE_COUNT),radicalInverseVdC(i));
        vec3 lightPoint=vec3(mix(-0.55,0.55,uv.x),2.464,mix(-1.48,-0.79,uv.y));
        vec3 toLight=lightPoint-p;
        float distance=length(toLight);
        vec3 l=toLight/max(distance,1e-5);
        float ndotl=max(dot(n,l),0.0);
        if (ndotl>0.0) {
            float visibility=shadowVisibility(p,n,geometricNormal,lightPoint,distance);
            if (visibility<=0.0) continue;
            vec3 radiance=vec3(14.0,13.4,12.3)/(1.0+0.12*distance*distance);
            vec3 reflectedLight=reflect(-l,n);
            float specular=max(dot(reflectedLight,viewDirection),0.0);
            direct+=(kd*ndotl+ks*pow(specular,shininess))*radiance*visibility;
        }
    }
    return ambient+direct/float(SAMPLE_COUNT);
}

vec3 shadeOpaque(Hit h, vec3 incoming) {
    if (!h.found) return vec3(0.012,0.016,0.024);
    if (h.material==5u) return vec3(12.0,11.5,10.5);
    vec3 n=dot(h.normal,incoming)>0.0 ? -h.normal : h.normal;
    return phongLighting(h.position,n,h.geometricNormal,normalize(-incoming),h.material);
}

vec3 shadeGlass(Hit entry, vec3 incoming) {
    vec3 n=dot(entry.normal,incoming)>0.0 ? -entry.normal : entry.normal;
    const float ior=1.5;
    vec3 insideDirection=refract(incoming,n,1.0/ior);
    vec3 transmitted=vec3(0.01);
    if (dot(insideDirection,insideDirection)>1e-6) {
        Hit exitHit=traceScene(entry.position+insideDirection*0.003,insideDirection,1000.0);
        if (exitHit.found && exitHit.material==4u) {
            vec3 exitNormal=dot(exitHit.normal,insideDirection)>0.0 ? -exitHit.normal : exitHit.normal;
            vec3 outsideDirection=refract(insideDirection,exitNormal,ior);
            if (dot(outsideDirection,outsideDirection)>1e-6) {
                Hit behind=traceScene(exitHit.position+outsideDirection*0.003,normalize(outsideDirection),1000.0);
                transmitted=shadeOpaque(behind,outsideDirection);
            }
        }
    }

    vec3 reflectedDirection=reflect(incoming,n);
    Hit reflectedHit=traceScene(entry.position+reflectedDirection*0.003,reflectedDirection,1000.0);
    vec3 reflected=shadeOpaque(reflectedHit,reflectedDirection);
    float cosTheta=clamp(dot(-incoming,n),0.0,1.0);
    float fresnel=0.04+0.96*pow(1.0-cosTheta,5.0);
    return mix(transmitted,reflected,fresnel);
}

void main() {
    vec2 uv=gl_FragCoord.xy/vec2(params.width,params.height);
    vec2 screen=vec2((uv.x*2.0-1.0)*(params.width/params.height),1.0-uv.y*2.0);
    vec3 origin=vec3(0.0,0.95,7.0);
    vec3 direction=normalize(vec3(screen*1.45,-4.6));
    Hit primary=traceScene(origin,direction,1000.0);
    vec3 color=primary.found && primary.material==4u
        ? shadeGlass(primary,direction) : shadeOpaque(primary,direction);
    color=color/(vec3(1.0)+color);
    /* The preferred swapchain is *_SRGB, so Vulkan performs linear-to-sRGB
     * conversion on store. Do not apply a second gamma transform here. */
    outColor=vec4(color,1.0);
}
